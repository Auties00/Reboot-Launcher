#include "backbone/nats.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

#include "core/hash.hpp"
#include "ops/log.hpp"
#include "registry/events.hpp"

namespace sb::backbone {

using registry::BackboneAck;
using registry::BackboneEdgeDown;
using registry::BackboneRecord;
using registry::BackboneSoft;
using registry::kNoShard;
using registry::ReplicaMsg;

namespace {

constexpr int64_t kTombstoneTtlMs = 30ll * 24 * 3600 * 1000;
constexpr char kOpHeader[] = "Sb-Op";

std::string hex_id(const Uuid& id) {
    static constexpr char d[] = "0123456789abcdef";
    std::string s;
    s.reserve(32);
    for (u8 b : id.bytes) {
        s.push_back(d[b >> 4]);
        s.push_back(d[b & 0xF]);
    }
    return s;
}

std::optional<Uuid> id_from_subject(std::string_view subj) {
    const auto dot = subj.rfind('.');
    if (dot == std::string_view::npos) return std::nullopt;
    return Uuid::parse(subj.substr(dot + 1));
}

std::string edge_key(u64 edge) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(edge));
    return buf;
}

}  // namespace

NatsBackbone::NatsBackbone(const edge::BackboneConfig& cfg, u64 edge_id)
    : cfg_(cfg), edge_id_(edge_id), reg_prefix_(cfg.subject_prefix + ".reg."), live_prefix_(cfg.subject_prefix + ".live.") {}

NatsBackbone::~NatsBackbone() { stop(); }

void NatsBackbone::check(natsStatus s, const char* what) {
    if (s != NATS_OK)
        throw std::runtime_error(std::string("nats: ") + what + ": " + natsStatus_GetText(s) + " " + nats_GetLastError(nullptr));
}

natsOptions* NatsBackbone::make_options(bool live) {
    natsOptions* o = nullptr;
    check(natsOptions_Create(&o), "options");
    std::vector<const char*> urls;
    for (const auto& u : cfg_.urls) urls.push_back(u.c_str());
    check(natsOptions_SetServers(o, urls.data(), static_cast<int>(urls.size())), "servers");
    natsOptions_SetName(o, live ? "sb-edge-live" : "sb-edge-js");
    natsOptions_SetMaxReconnect(o, -1);
    natsOptions_SetReconnectWait(o, 250);
    natsOptions_SetRetryOnFailedConnect(o, true, nullptr, nullptr);
    if (live) {
        natsOptions_SetSendAsap(o, true);  // no flusher hop: each publish goes straight to the socket
        natsOptions_SetNoEcho(o, true);    // never receive our own soft updates
    }
    if (!cfg_.tls_ca.empty()) {
        check(natsOptions_SetSecure(o, true), "tls");
        check(natsOptions_LoadCATrustedCertificates(o, cfg_.tls_ca.c_str()), "tls ca");
        if (!cfg_.tls_cert.empty())
            check(natsOptions_LoadCertificatesChain(o, cfg_.tls_cert.c_str(), cfg_.tls_key.c_str()), "tls cert");
    }
    return o;
}

void NatsBackbone::start(Sink sink) {
    sink_ = std::move(sink);
    natsOptions* jo = make_options(false);
    natsOptions* lo = make_options(true);
    const natsStatus s1 = natsConnection_Connect(&js_conn_, jo);
    const natsStatus s2 = natsConnection_Connect(&live_conn_, lo);
    natsOptions_Destroy(jo);
    natsOptions_Destroy(lo);
    check(s1, "connect");
    check(s2, "connect live");

    jsOptions jso;
    jsOptions_Init(&jso);
    jso.PublishAsync.MaxPending = 8192;
    jso.PublishAsync.AckHandler = &NatsBackbone::on_ack;
    jso.PublishAsync.AckHandlerClosure = this;
    check(natsConnection_JetStream(&js_, js_conn_, &jso), "jetstream");

    // Lifecycle stream: one retained message (latest record or tombstone) per entry.
    const std::string subjects = reg_prefix_ + "*";
    const char* subj[] = {subjects.c_str()};
    jsStreamConfig sc;
    jsStreamConfig_Init(&sc);
    sc.Name = cfg_.stream.c_str();
    sc.Subjects = subj;
    sc.SubjectsLen = 1;
    sc.MaxMsgsPerSubject = 1;
    sc.Storage = js_FileStorage;
    sc.Replicas = static_cast<int64_t>(cfg_.replicas);
    sc.AllowMsgTTL = true;
    jsStreamInfo* si = nullptr;
    jsErrCode jerr = static_cast<jsErrCode>(0);
    if (js_AddStream(&si, js_, &sc, nullptr, &jerr) != NATS_OK) {
        // Usually it already exists, created by another edge.
        check(js_GetStreamInfo(&si, js_, cfg_.stream.c_str(), nullptr, &jerr), "create or find lifecycle stream");
    }
    jsStreamInfo_Destroy(si);

    kvConfig kc;
    kvConfig_Init(&kc);
    kc.Bucket = cfg_.lease_bucket.c_str();
    kc.History = 1;
    kc.TTL = static_cast<int64_t>(cfg_.lease_ttl_ms) * 1'000'000;  // nanoseconds
    kc.Replicas = static_cast<int>(cfg_.replicas);
    if (js_CreateKeyValue(&leases_, js_, &kc) != NATS_OK) check(js_KeyValue(&leases_, js_, cfg_.lease_bucket.c_str()), "lease bucket");

    // Soft path first so nothing published during the replay is missed; the replica holds early
    // soft updates until their record arrives.
    const std::string live_subj = live_prefix_ + "*";
    check(natsConnection_Subscribe(&live_sub_, live_conn_, live_subj.c_str(), &NatsBackbone::on_live, this), "live subscribe");

    jsSubOptions so;
    jsSubOptions_Init(&so);
    so.Ordered = true;
    so.Config.DeliverPolicy = js_DeliverLastPerSubject;
    so.Stream = cfg_.stream.c_str();
    check(js_Subscribe(&records_sub_, js_, subjects.c_str(), &NatsBackbone::on_record, this, nullptr, &so, &jerr), "record subscribe");

    // An empty stream never delivers a message with NumPending == 0.
    if (js_GetStreamInfo(&si, js_, cfg_.stream.c_str(), nullptr, &jerr) == NATS_OK) {
        if (si->State.Msgs == 0) caught_up_.store(true, std::memory_order_release);
        jsStreamInfo_Destroy(si);
    }

    lease_watcher_ = std::jthread([this](std::stop_token st) { watch_leases(st); });
    log::info("nats backbone connected: stream {}, leases {}", cfg_.stream, cfg_.lease_bucket);
}

void NatsBackbone::stop() {
    lease_watcher_ = {};
    if (records_sub_) {
        natsSubscription_Unsubscribe(records_sub_);
        natsSubscription_Destroy(records_sub_);
        records_sub_ = nullptr;
    }
    if (live_sub_) {
        natsSubscription_Unsubscribe(live_sub_);
        natsSubscription_Destroy(live_sub_);
        live_sub_ = nullptr;
    }
    if (js_) {
        jsPubOptions po;
        jsPubOptions_Init(&po);
        po.MaxWait = 2000;
        (void)js_PublishAsyncComplete(js_, &po);
    }
    if (leases_) {
        (void)kvStore_Delete(leases_, edge_key(edge_id_).c_str());
        kvStore_Destroy(leases_);
        leases_ = nullptr;
    }
    if (js_) {
        jsCtx_Destroy(js_);
        js_ = nullptr;
    }
    if (live_conn_) {
        natsConnection_Destroy(live_conn_);
        live_conn_ = nullptr;
    }
    if (js_conn_) {
        natsConnection_Destroy(js_conn_);
        js_conn_ = nullptr;
    }
}

void NatsBackbone::publish_record(const Uuid& id, std::vector<u8> payload, u64 expected_seq, u64 op) {
    const std::string subj = reg_prefix_ + hex_id(id);
    natsMsg* msg = nullptr;
    if (natsMsg_Create(&msg, subj.c_str(), nullptr, reinterpret_cast<const char*>(payload.data()), static_cast<int>(payload.size())) != NATS_OK) {
        sink_(new ReplicaMsg(kNoShard, 0, BackboneAck{.op = op, .ok = false}));
        return;
    }
    natsMsgHeader_Set(msg, kOpHeader, std::to_string(op).c_str());
    jsPubOptions po;
    jsPubOptions_Init(&po);
    if (expected_seq == 0) po.ExpectNoMessage = true;
    else po.ExpectLastSubjectSeq = expected_seq;
    if (payload.empty()) po.MsgTTL = kTombstoneTtlMs;  // tombstones age out with the id reservation
    if (js_PublishMsgAsync(js_, &msg, &po) != NATS_OK) {
        natsMsg_Destroy(msg);
        sink_(new ReplicaMsg(kNoShard, 0, BackboneAck{.op = op, .ok = false}));
    }
}

void NatsBackbone::publish_soft(const Uuid& id, std::vector<u8> payload) {
    const std::string subj = live_prefix_ + hex_id(id);
    (void)natsConnection_Publish(live_conn_, subj.c_str(), payload.data(), static_cast<int>(payload.size()));
}

void NatsBackbone::heartbeat_lease(u64 edge_id) {
    if (!leases_) return;
    u64 rev = 0;
    (void)kvStore_PutString(&rev, leases_, edge_key(edge_id).c_str(), "1");
}

void NatsBackbone::on_ack(jsCtx*, natsMsg* msg, jsPubAck* pa, jsPubAckErr* pae, void* closure) {
    auto* self = static_cast<NatsBackbone*>(closure);
    const char* opv = nullptr;
    u64 op = 0;
    if (natsMsgHeader_Get(msg, kOpHeader, &opv) == NATS_OK && opv) op = std::strtoull(opv, nullptr, 10);
    if (pa) {
        self->sink_(new ReplicaMsg(kNoShard, 0, BackboneAck{.op = op, .ok = true, .stream_seq = pa->Sequence}));
    } else {
        if (pae && pae->ErrText) log::debug("lifecycle write rejected: {}", pae->ErrText);
        self->sink_(new ReplicaMsg(kNoShard, 0, BackboneAck{.op = op, .ok = false}));
    }
    natsMsg_Destroy(msg);
}

void NatsBackbone::on_record(natsConnection*, natsSubscription*, natsMsg* msg, void* closure) {
    auto* self = static_cast<NatsBackbone*>(closure);
    jsMsgMetaData* meta = nullptr;
    if (natsMsg_GetMetaData(&meta, msg) == NATS_OK) {
        if (auto id = id_from_subject(natsMsg_GetSubject(msg))) {
            const auto* data = reinterpret_cast<const u8*>(natsMsg_GetData(msg));
            BackboneRecord rec{.stream_seq = meta->Sequence.Stream,
                               .payload = std::vector<u8>(data, data + natsMsg_GetDataLength(msg)),
                               .id = *id};
            self->sink_(new ReplicaMsg(kNoShard, 0, std::move(rec)));
        }
        if (meta->NumPending == 0) self->caught_up_.store(true, std::memory_order_release);
        jsMsgMetaData_Destroy(meta);
    }
    natsMsg_Destroy(msg);
}

void NatsBackbone::on_live(natsConnection*, natsSubscription*, natsMsg* msg, void* closure) {
    auto* self = static_cast<NatsBackbone*>(closure);
    const auto* data = reinterpret_cast<const u8*>(natsMsg_GetData(msg));
    self->sink_(new ReplicaMsg(kNoShard, 0, BackboneSoft{.payload = std::vector<u8>(data, data + natsMsg_GetDataLength(msg))}));
    natsMsg_Destroy(msg);
}

void NatsBackbone::watch_leases(std::stop_token stop) {
    bool first = true;
    while (!stop.stop_requested()) {
        kvKeysList keys{};
        const natsStatus st = kvStore_Keys(&keys, leases_, nullptr);
        if (st == NATS_OK || st == NATS_NOT_FOUND) {
            std::set<u64> now;
            for (int i = 0; i < keys.Count; ++i) now.insert(std::strtoull(keys.Keys[i], nullptr, 16));
            kvKeysList_Destroy(&keys);
            if (!first) {
                for (u64 dead : live_edges_) {
                    if (now.contains(dead) || dead == edge_id_) continue;
                    // Rendezvous hashing: every survivor elects the same reaper.
                    u64 best = 0, best_score = 0;
                    for (u64 e : now) {
                        const u64 score = mix64(e ^ mix64(dead));
                        if (score >= best_score) {
                            best_score = score;
                            best = e;
                        }
                    }
                    log::warn("edge {:016x} lease expired; reaper {:016x}", dead, best);
                    sink_(new ReplicaMsg(kNoShard, 0, BackboneEdgeDown{.edge_id = dead, .reap = best == edge_id_}));
                }
            }
            live_edges_ = std::move(now);
            first = false;
        }
        for (int i = 0; i < 10 && !stop.stop_requested(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

}  // namespace sb::backbone
