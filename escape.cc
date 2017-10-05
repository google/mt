#include "escape.h"

// TODO: unify this with UTF8 logic elsewhere, or use a common library?
static void Encode(std::string* out, uint32_t rune) {
  if (rune < 0x80) return out->push_back(rune);
  if (rune < 0x800) {
    out->push_back(0xc0 | (rune >> 6));
    return out->push_back(0x80 | (rune & 0x3f));
  }
  if (rune < 0x10000) {
    out->push_back(0xe0 | (rune >> 12));
    out->push_back(0x80 | ((rune >> 6) & 0x3f));
    return out->push_back(0x80 | (rune & 0x3f));
  }
  if (rune < 0x110000) {
    out->push_back(0xf0 | rune >> 18);
    out->push_back(0x80 | ((rune >> 12) & 0x3f));
    out->push_back(0x80 | ((rune >> 6) & 0x3f));
    return out->push_back(0x80 | (rune & 0x3f));
  }
  // Invalid codepoint, ignore.
}

void EscapeParser::Handle(uint32_t rune) {
  // Most logic considers 0xa0-0xff equivalent to 0x20-0x7f.
  // Exception: when recording a string, we record the original (UTF-8 encoded).
  uint32_t c = (rune >= 0xa0 && rune < 0x100) ? rune & 0x7f : rune;
  // Some characters are handled the same way in all modes.
  switch (c) {
    case 0x1B:
      return Transition(ESCAPE);
    case 0x90:
      return Transition(DCS_ENTRY);
    case 0x9B:
      return Transition(CSI_ENTRY);
    case 0x9C:
      return Transition(GROUND);
    case 0x9D:
      return Transition(OSC_STRING);
    case 0x98:
    case 0x9E:
    case 0x9F:
      return Transition(SOS_PM_APC_STRING);
    case 0x18:
    case 0x1A:
    case 0x80:
    case 0x81:
    case 0x82:
    case 0x83:
    case 0x84:
    case 0x85:
    case 0x86:
    case 0x87:
    case 0x88:
    case 0x89:
    case 0x8a:
    case 0x8b:
    case 0x8c:
    case 0x8d:
    case 0x8e:
    case 0x8f:
    case 0x91:
    case 0x92:
    case 0x93:
    case 0x94:
    case 0x95:
    case 0x96:
    case 0x97:
    case 0x99:
    case 0x9a:
      return Transition(GROUND, [&] { actions_->Control(c); });
    case 0x7f:
      if (state_ != OSC_STRING) return;
  }
  // C0 control characters (not handled above) have uniform rules per state.
  if (c < 0x20) {
    switch (__builtin_expect(state_, GROUND)) {
      case GROUND:
      case ESCAPE:
      case ESCAPE_INTERMEDIATE:
      case CSI_ENTRY:
      case CSI_INTERMEDIATE:
      case CSI_PARAM:
      case CSI_IGNORE:
        return actions_->Control(c);
      case DCS_PASSTHROUGH:
        return Encode(&payload_, rune);
      default:
        return;
    }
  }
  switch (state_) {
    case ESCAPE:
      switch (c) {
        case 0x4B:
          return Transition(TITLE_LEGACY);
        case 0x50:
          return Transition(DCS_ENTRY);
        case 0x5B:
          return Transition(CSI_ENTRY);
        case 0x58:
        case 0x5E:
        case 0x5F:
          return Transition(SOS_PM_APC_STRING);
        case 0x5D:
          return Transition(OSC_STRING);
          // TODO: legacy set title? ESC k <string> ST
      }
    /* fallthrough */
    case ESCAPE_INTERMEDIATE:
      if (c < 0x30)
        return Transition(ESCAPE_INTERMEDIATE,
                          [&] { Encode(&command_, rune); });
      return Transition(GROUND, [&] {
        Encode(&command_, rune);
        actions_->Escape(command_);
      });
    case CSI_ENTRY:
      if (c > 0x3a && c < 0x40 && c != ';')
        return Transition(CSI_PARAM, [&] { Encode(&command_, rune); });
      /* fallthrough */
    case CSI_PARAM:
      if (ParamParse(c)) return Transition(CSI_PARAM);
      /* fallthrough */
    case CSI_INTERMEDIATE:
      Encode(&command_, rune);
      if (c >= 0x40)
        return Transition(GROUND, [&] { actions_->CSI(command_, args_); });
      if (c < 0x30) return Transition(CSI_INTERMEDIATE);
      return Transition(CSI_IGNORE);
    case CSI_IGNORE:
      if (c >= 0x40) return Transition(GROUND);
      return;
    case DCS_ENTRY:
      if (c > 0x3a && c < 0x40 && c != ';')
        return Transition(CSI_PARAM, [&] { Encode(&command_, rune); });
      /* fallthrough */
    case DCS_PARAM:
      if (ParamParse(c)) return Transition(DCS_PARAM);
      /* fallthrough */
    case DCS_INTERMEDIATE:
      if (c >= 0x40)
        return Transition(DCS_PASSTHROUGH, [&] { Encode(&command_, rune); });
      if (c < 0x30)
        return Transition(DCS_INTERMEDIATE, [&] { Encode(&command_, rune); });
      return Transition(DCS_IGNORE);
    case DCS_PASSTHROUGH:
    case TITLE_LEGACY:
      return Encode(&payload_, rune);
    case DCS_IGNORE:
      if (c == 0x9c) return Transition(GROUND);
      return;
    case OSC_STRING:
      return Encode(&command_, rune);
    case SOS_PM_APC_STRING:
      return;
  }
}

bool EscapeParser::ParamParse(uint32_t c) {
  // This diverges from the state machine: both vttest and existing terminals
  // expect <ESC>[4;m to be CSI(m, {4,0}), not CSI(;m, {4}).
  if (c == ';') {
    if (arg_in_progress_) {
      arg_in_progress_ = false;
    } else {
      args_.push_back(0);
    }
    return true;
  }
  if (c >= '0' && c <= '9') {
    if (!arg_in_progress_) {
      args_.push_back(0);
      arg_in_progress_ = true;
    }
    args_.back() *= 10;
    args_.back() += c - '0';
    return true;
  }
  return false;
}

void EscapeParser::Enter(State state) {
  state_ = state;
  // Entry actions.
  switch (state_) {
    case ESCAPE:
    case DCS_ENTRY:
    case CSI_ENTRY:
    case TITLE_LEGACY:
      return Clear();
  }
}

void EscapeParser::Clear() {
  command_.clear();
  payload_.clear();
  args_.clear();
  arg_in_progress_ = false;
}

void EscapeParser::Exit(State state) {
  switch (state_) {
    case OSC_STRING:
      actions_->OSC(command_);
      break;
    case DCS_PASSTHROUGH:
      actions_->DCS(command_, args_, payload_);
      break;
    case TITLE_LEGACY:
      actions_->SetTitle(payload_);
      break;
  }
}
