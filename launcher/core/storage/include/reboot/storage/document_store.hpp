#pragma once

#include <concepts>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/json/object.hpp>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/storage/document_file.hpp"
#include "reboot/storage/load_report.hpp"
#include "reboot/storage/storage_mode_changed.hpp"

namespace reboot::storage {

// read() never fails: a bad member becomes its default with a ValueIssue; unknown ones are kept.
template <class D>
concept Document = std::default_initializable<D> && std::copy_constructible<D> &&
                   requires(const D& document, const boost::json::object& values, boost::json::object old_values,
                            u32 from_schema, std::vector<ValueIssue>& issues) {
                       { D::kName } -> std::convertible_to<std::string_view>;
                       { D::kSchema } -> std::convertible_to<u32>;
                       { D::read(values, issues) } -> std::same_as<D>;
                       { document.write() } -> std::same_as<boost::json::object>;
                       { D::upgrade(std::move(old_values), from_schema) } -> std::same_as<Result<boost::json::object>>;
                   };

// Capabilities: settings-storage.layout.
// Strand-only. A DocumentFile that keeps its validated document in memory.
template <Document D>
class DocumentStore {
public:
    DocumentStore(ports::IFileSystem& fs, WorkerPool& workers, Executor& strand, const IClock& clock, NativePath path)
        : file_(fs, workers, strand, clock, std::move(path), DocumentFormat{D::kName, D::kSchema, &D::upgrade}) {
        file_.set_on_reload([this](const boost::json::object& values) {
            std::vector<ValueIssue> issues;
            document_ = D::read(values, issues);
            if (on_reload_) on_reload_(document_, issues);
        });
    }
    DocumentStore(const DocumentStore&) = delete;
    DocumentStore& operator=(const DocumentStore&) = delete;

    [[nodiscard]] LoadReport load() { return adopt(file_.load()); }
    [[nodiscard]] LoadReport load_memory_only(Diagnostic reason) {
        return adopt(file_.load_memory_only(std::move(reason)));
    }

    [[nodiscard]] const D& get() const noexcept { return document_; }
    [[nodiscard]] u64 revision() const noexcept { return file_.revision(); }
    [[nodiscard]] StorageMode mode() const noexcept { return file_.mode(); }

    // Applies `mutate` to a copy, kept only if the file accepts it; returns the new revision.
    Result<u64> update(UniqueFunction<void(D&)> mutate) {
        D next = document_;
        mutate(next);
        Result<u64> revision = file_.replace(next.write());
        if (revision) document_ = std::move(next);
        return revision;
    }

    void refresh() { file_.refresh(); }
    void flush(CancelToken cancel, UniqueFunction<void(Result<void>)> done) {
        file_.flush(std::move(cancel), std::move(done));
    }
    void set_on_reload(UniqueFunction<void(const D&, std::span<const ValueIssue>)> on_reload) {
        on_reload_ = std::move(on_reload);
    }
    void set_on_mode_changed(UniqueFunction<void(const StorageModeChanged&)> on_mode_changed) {
        file_.set_on_mode_changed(std::move(on_mode_changed));
    }

private:
    LoadReport adopt(LoadReport report) {
        document_ = D::read(file_.values(), report.issues);
        return report;
    }

    DocumentFile file_;
    D document_{};
    UniqueFunction<void(const D&, std::span<const ValueIssue>)> on_reload_;
};

}  // namespace reboot::storage
