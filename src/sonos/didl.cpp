#include "didl.h"

// TODO Phase 3: string-scan <item>/<container> blocks; pull dc:title, <res>, and the
//               per-item DIDL snippet. Unescape XML entities. Avoid a full DOM parser.

namespace sonos {

size_t parseDidl(const String&, std::vector<DidlItem>& out) {
  out.clear();
  return 0;  // TODO
}

}  // namespace sonos
