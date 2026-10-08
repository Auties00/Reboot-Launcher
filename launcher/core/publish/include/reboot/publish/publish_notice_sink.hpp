#pragma once

#include "reboot/publish/publish_notice.hpp"

namespace reboot::publish {

// The engine's guidance notices, given to HostPublisher at construction so no notice is lost.
class IPublishNoticeSink {
public:
    virtual ~IPublishNoticeSink() = default;
    // On the strand.
    virtual void on_publish_notice(const PublishNotice& notice) = 0;
};

}  // namespace reboot::publish
