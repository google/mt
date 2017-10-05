#ifndef MT_ESCAPE_H_
#define MT_ESCAPE_H_

#include <string>
#include <vector>

// Parser for terminal escape sequences.
// Based on the state machine described at https://vt100.net/emu/dec_ansi_parser
//
// Pluggable parsers for OSC and DCS are not implemented - whole strings are
// captured instead. SOS/PM/APC commands are recognized but ignored.
class EscapeParser {
 public:
  // Interface for callbacks when we encounter various escape sequences.
  class Actions {
   public:
    virtual ~Actions() = default;
    // A C0 or C1 control function appearing in the stream.
    virtual void Control(uint8_t control) = 0;
    // An escape sequence: <esc> command
    virtual void Escape(const std::string& command) = 0;
    // A control sequence: <esc> [ arg1 ; arg2 command
    virtual void CSI(const std::string& command,
                     const std::vector<int>& args) = 0;
    // Device control string: <esc> P arg1 ; arg2 command payload <9C>
    virtual void DCS(const std::string& command, const std::vector<int>& args,
                     const std::string& payload) = 0;
    // Operating system command: <esc> ] command <9C>
    virtual void OSC(const std::string& command) = 0;
    // Set title (legacy): <esc> K payload <9C>
    virtual void SetTitle(const std::string& payload) = 0;
  };

  EscapeParser(Actions* actions) : actions_(actions) { Clear(); }

  // Feed the parser a unicode codepoint.
  // Returns false if it should be printed (this is the common case).
  inline bool Consume(uint32_t rune) {
    if (state_ == GROUND) {
      if (rune >= 0x20 && rune < 0x80) return false;
      if (rune >= 0xa0) return false;
    }
    Handle(rune);
    return true;
  }

 private:
  enum State {
    GROUND,
    OSC_STRING,
    SOS_PM_APC_STRING,
    ESCAPE,
    ESCAPE_INTERMEDIATE,
    TITLE_LEGACY,
    CSI_ENTRY,
    CSI_INTERMEDIATE,
    CSI_PARAM,
    CSI_IGNORE,
    DCS_ENTRY,
    DCS_INTERMEDIATE,
    DCS_PARAM,
    DCS_PASSTHROUGH,
    DCS_IGNORE,
  };

  void Clear();
  void Handle(uint32_t rune);
  bool ParamParse(uint32_t c);

  struct Ignore {
    void operator()() const {}
  };
  // Transition to another state.
  // Runs the exit, transition, and enter actions appropriately.
  template <typename Action = Ignore>
  void Transition(State state, const Action& transition_action = Action())
      __attribute__((always_inline)) {
    Exit(state_);
    transition_action();
    Enter(state);
    state_ = state;
  }
  void Enter(State state);
  void Exit(State state);

  State state_ = GROUND;
  Actions* actions_;
  std::string command_;
  std::string payload_;
  std::vector<int> args_;
  bool arg_in_progress_ = false;
};

// Dumps all received actions to stderr.
// Useful for debugging the parser or as a fallback for unhandled actions.
class DebugActions : public EscapeParser::Actions {
 public:
  void Control(uint8_t control) override {
    fprintf(stderr, "Control(%02x)\n", control);
  }
  void Escape(const std::string& command) override {
    fprintf(stderr, "Esc(%s)\n", command.c_str());
  }
  void SetTitle(const std::string& payload) override {
    fprintf(stderr, "SetTitle(%s)\n", payload.c_str());
  }
  void CSI(const std::string& command, const std::vector<int>& args) override {
    fprintf(stderr, "CSI(%s, %s)\n", command.c_str(), Join(args).c_str());
  }
  void DCS(const std::string& command, const std::vector<int>& args,
           const std::string& payload) override {
    fprintf(stderr, "DCS(%s, %s, %s)\n", command.c_str(), Join(args).c_str(),
            payload.c_str());
  }
  void OSC(const std::string& command) override {
    fprintf(stderr, "OSC(%s)\n", command.c_str());
  }

 private:
  static std::string Join(const std::vector<int>& args) {
    std::string result = "[";
    for (int i = 0; i < args.size(); ++i) {
      if (i) result.push_back(',');
      result.append(std::to_string(args[i]));
    }
    result.push_back(']');
    return result;
  }
};

#endif
