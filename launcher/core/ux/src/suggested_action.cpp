#include "reboot/ux/suggested_action.hpp"

#include "links.hpp"
#include "messages.hpp"

namespace rb::ux {

namespace {

struct LabelOf {
    MessageId operator()(const RemediatePrerequisite&) const { return msg::kActionRemediatePrerequisite; }
    MessageId operator()(const InstallBuild&) const { return msg::kActionInstallBuild; }
    MessageId operator()(const ImportBuild&) const { return msg::kActionImportBuild; }
    MessageId operator()(const EditDisplayName&) const { return msg::kActionEditDisplayName; }
    MessageId operator()(const SetDefaultHostListing& action) const {
        return action.listed ? msg::kChoiceListPublicly : msg::kChoiceKeepUnlisted;
    }
    MessageId operator()(const ListHostProfile&) const { return msg::kActionListHostProfile; }
    MessageId operator()(const CopyShareLink&) const { return msg::kActionCopyShareLink; }
    MessageId operator()(const OpenAppLink& action) const { return app_link_label(action.link); }
};

}  // namespace

MessageId action_label(const SuggestedAction& action) { return std::visit(LabelOf{}, action); }

}  // namespace rb::ux
