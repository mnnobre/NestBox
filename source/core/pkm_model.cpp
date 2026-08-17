#include "pkm_model.h"

namespace pkm {

const char* FormatName(Format f) {
  switch (f) {
    case Format::kPB7: return "PB7";
    case Format::kPK8: return "PK8";
    case Format::kPB8: return "PB8";
    case Format::kPA8: return "PA8";
    case Format::kPK9: return "PK9";
    case Format::kNone: break;
  }
  return "?";
}

}  // namespace pkm
