#include "reboot/support/cell_inputs.hpp"

#include <boost/pfr/ops_fields.hpp>

namespace rb::support {

bool PlayCellInputs::operator==(const PlayCellInputs& other) const noexcept {
    // Field-wise by reflection, so a field added to ContentVersion is compared too.
    return client_dll_sha256 == other.client_dll_sha256 &&
           boost::pfr::eq_fields(backend_content, other.backend_content) && runner_pin == other.runner_pin;
}

}  // namespace rb::support
