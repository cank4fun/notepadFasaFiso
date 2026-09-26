#pragma once

#if defined(NFF_ENABLE_GUI_AUTOMATION) && NFF_ENABLE_GUI_AUTOMATION

#include "notepadFasaFiso/gui/automation/AutomationProtocol.hpp"

#include <vector>

class wxWindow;

namespace nff::gui::wxbackend {

[[nodiscard]] std::vector<automation::AutomationElementSnapshot>
collectNamedAutomationElements(wxWindow& root);

[[nodiscard]] automation::AutomationWindowSnapshot
collectAutomationWindowSnapshot(wxWindow& root);

}

#endif
