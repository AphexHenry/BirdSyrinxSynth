#pragma once

namespace syrinx {

/// WP-R4: in the Standalone app, switch on the MIDI inputs (a MIDI keyboard plays at once) when the user
/// has not chosen any yet. Elsewhere (plugin formats, tools, tests) it does nothing. Message thread.
/// Returns the number of inputs switched on.
int enableMidiInputsInStandalone();

} // namespace syrinx
