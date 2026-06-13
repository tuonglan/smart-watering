// Terminal — debug WidgetTerminal on V41 with a small command framework.
//
//  V41 (WidgetTerminal): you type a command; output is printed back to the SAME
//    widget. A WidgetTerminal uses ONE virtual pin for BOTH directions — app->device
//    text arrives as BLYNK_WRITE(V41); device->app text (terminal.print + flush) is
//    appended to that same widget's scrollback. There is no second "stream" to wire.
//  V42 (Switch): run mode. 1 = re-run the entered command continuously at 1 Hz (a
//    live tail) until you clear it (empty line), flip V42 off, or the 5-min quota
//    guard fires; 0 = run the command once per entry.
//
//  Caching / optimisation: the typed line is parsed ONCE in onInput() — the command
//  is looked up in a table and its args copied into the manager. The 1 Hz tick then
//  re-runs the cached command pointer with the cached args, doing NO string parsing
//  or lookup per tick.
//
//  Design: an abstract TermCommand base + one tiny derived class per command, held in
//  a lookup table. Adding a command later = new derived class + one row in the table.
//
//  Dependency: the 3rd-party "ESP32Ping" library (Arduino Library Manager: search
//  "ESP32Ping", by Marian Craciunescu). Ping.ping() is blocking (up to ~1 s with no
//  reply); at 1 Hz that briefly stalls loop(), but the relay esp_timer failsafe is
//  independent of loop() so the pumps stay safe.

#pragma once

#include <ESP32Ping.h>
#include "Moisture.h"        // MoistureConfig::readRaw()

#ifndef VPIN_TERMINAL
#define VPIN_TERMINAL  V41
#endif
#ifndef VPIN_TERM_RERUN
#define VPIN_TERM_RERUN  V43   // Switch: re-run the last command (see TerminalManager)
#endif

// The brief command guide — single source of truth shared by the boot-time print
// (TerminalManager::printHelp) and the "help" command. Prints lines only; the caller
// flushes (so it batches into one Blynk message with surrounding output).
static void termPrintGuide(WidgetTerminal &term) {
  term.println("=== debug terminal (V41) ===");
  term.println("V42=1: run @1Hz, auto-stop 5m | V42=0: run once");
  term.println("V43: re-run last command");
  term.println(" ping <ip|host>   ICMP echo, RTT in ms");
  term.println(" get_moisture     raw ADC s0/s1/s2 (GPIO4/5/6)");
  term.println(" help             show this guide");
  term.println("empty line = stop");
}

// ------------------------------------------------------------------ //
//  Command base + concrete commands                                    //
// ------------------------------------------------------------------ //

class TermCommand {
public:
  virtual ~TermCommand() {}
  virtual const char *name() const = 0;
  // Run with `args` = the text after the command name (trimmed; "" if none). Print
  // human-readable output to `term`; the manager flushes afterwards. Called once per
  // entry, plus every 1 Hz tick while in continuous mode.
  virtual void run(const char *args, WidgetTerminal &term) = 0;
};

// ping <ip|host> — one ICMP echo, prints the round-trip time (or "timeout").
class PingCommand : public TermCommand {
public:
  const char *name() const override { return "ping"; }
  void run(const char *args, WidgetTerminal &term) override {
    if (!args[0]) { term.println("usage: ping <ip|host>"); return; }
    char line[96];
    if (Ping.ping(args, 1)) {
      snprintf(line, sizeof(line), "ping %s : %.1f ms", args, Ping.averageTime());
    } else {
      snprintf(line, sizeof(line), "ping %s : timeout", args);
    }
    term.println(line);
  }
};

// get_moisture — raw averaged ADC for all three channels (s0/s1/s2 = GPIO4/5/6),
// regardless of what V11 selected. Zero-padded to 4 digits so columns line up.
class GetMoistureCommand : public TermCommand {
public:
  const char *name() const override { return "get_moisture"; }
  void run(const char * /*args*/, WidgetTerminal &term) override {
    char line[48];
    snprintf(line, sizeof(line), "s0=%04d  s1=%04d  s2=%04d",
             MoistureConfig::readRaw(0), MoistureConfig::readRaw(1), MoistureConfig::readRaw(2));
    term.println(line);
  }
};

// help — reprint the command guide (same text shown once at boot).
class HelpCommand : public TermCommand {
public:
  const char *name() const override { return "help"; }
  void run(const char * /*args*/, WidgetTerminal &term) override { termPrintGuide(term); }
};

// ------------------------------------------------------------------ //
//  TerminalManager — input parsing, command cache, 1 Hz execution      //
// ------------------------------------------------------------------ //

class TerminalManager {
public:
  TerminalManager() : _term(VPIN_TERMINAL), _active(nullptr), _lastCmd(nullptr), _lastChangeMs(0) {
    _table[0] = &_ping;
    _table[1] = &_moist;
    _table[2] = &_help;
    _args[0]  = '\0';
  }

  // app->device line from BLYNK_WRITE(V41). `continuous` is the current V42 mode.
  // Parses + caches the command, then runs it once for immediate feedback. In
  // continuous mode it stays cached so tick() keeps re-running it.
  void onInput(const char *line, bool continuous) {
    char buf[CMD_MAX];
    _copyTrim(buf, line, sizeof(buf));

    if (buf[0] == '\0') {            // empty line = stop, no further output
      if (_active) {
        LOG_INFO("terminal: cleared by empty input — stopped");
        Blynk.virtualWrite(VPIN_TERM_RERUN, 0);   // release the re-run button too
      } else {
        DEBUG_PRINT("terminal: empty input (nothing running)");
      }
      _active = nullptr;
      return;
    }

    // (No echo: the Terminal widget already prints your input prefixed ">" and device
    // output prefixed "<", so an echo here would just double the command line.)

    // Split "<name> <args>" in place.
    char *args = buf;
    while (*args && *args != ' ') args++;
    if (*args == ' ') { *args++ = '\0'; while (*args == ' ') args++; }

    TermCommand *cmd = _find(buf);
    if (!cmd) {
      _term.println("unknown command — type 'help'");
      _term.flush();
      LOG_WARN(String("terminal: unknown command '") + buf + "'");
      _active = nullptr;
      return;
    }

    strncpy(_args, args, sizeof(_args) - 1);
    _args[sizeof(_args) - 1] = '\0';
    _lastChangeMs = millis();
    _lastCmd      = cmd;             // remember it for the V43 re-run button

    LOG_INFO(String("terminal: run '") + buf + (_args[0] ? String(" ") + _args : String("")) +
             "' (" + (continuous ? "continuous @1Hz" : "once") + ")");

    cmd->run(_args, _term);          // immediate feedback (both modes)
    _term.flush();

    _active = continuous ? cmd : nullptr;   // keep running only in continuous mode
  }

  // 1 Hz tick. `continuous` is the current V42 mode. Idle (returns immediately) unless
  // a continuous command is cached.
  void tick(bool continuous) {
    if (!_active) return;

    if (!continuous) {               // V42 switched off mid-run -> stop the live tail
      _term.println("[stopped]");
      _term.flush();
      LOG_INFO("terminal: live tail stopped (V42 off)");
      _active = nullptr;
      Blynk.virtualWrite(VPIN_TERM_RERUN, 0);       // release the re-run button too
      return;
    }

    if (millis() - _lastChangeMs >= TIMEOUT_MS) {   // forgotten-command quota guard
      _term.println("[auto-stop after 5 min]");
      _term.flush();
      LOG_WARN("terminal: auto-stopped after 5 min (quota guard)");
      _active = nullptr;
      Blynk.virtualWrite(VPIN_TERMINAL, "");        // clear the command on Blynk too
      Blynk.virtualWrite(VPIN_TERM_RERUN, 0);       // release the re-run button too
      return;
    }

    _active->run(_args, _term);
    _term.flush();
  }

  // V43 pressed (->1): re-run the last entered command (see _lastCmd). `continuous` is
  // the current V42 mode. Once: run a single time, then release the button (V43->0).
  // Continuous: (re)start the live tail, button stays 1 until pressed again (->0) or the
  // 5-min guard fires. No-op (with a note) if nothing has been run yet this boot.
  void rerun(bool continuous) {
    if (!_lastCmd) {
      _term.println("no command to re-run yet");
      _term.flush();
      LOG_WARN("terminal: re-run with no previous command");
      Blynk.virtualWrite(VPIN_TERM_RERUN, 0);       // nothing to keep the button on for
      return;
    }

    _lastChangeMs = millis();                       // restart the 5-min window
    LOG_INFO(String("terminal: re-run '") + _lastCmd->name() +
             (_args[0] ? String(" ") + _args : String("")) +
             "' (" + (continuous ? "continuous @1Hz" : "once") + ")");

    _lastCmd->run(_args, _term);
    _term.flush();

    if (continuous) {
      _active = _lastCmd;                           // keep tailing; V43 stays 1
    } else {
      _active = nullptr;
      Blynk.virtualWrite(VPIN_TERM_RERUN, 0);       // once: auto-release the button
    }
  }

  // V43 released (->0): stop a running re-run / live tail (the "tap again" stop).
  void stopRerun() {
    if (_active) {
      _term.println("[stopped]");
      _term.flush();
      LOG_INFO("terminal: re-run stopped (V43 off)");
      _active = nullptr;
    }
  }

  // Brief command guide. Printed once, on the first connection after boot.
  void printHelp() {
    termPrintGuide(_term);
    _term.flush();
  }

private:
  static const size_t   CMD_MAX    = 64;
  static const uint32_t TIMEOUT_MS = 5UL * 60UL * 1000UL;   // 5 min quota guard

  WidgetTerminal     _term;
  PingCommand        _ping;
  GetMoistureCommand _moist;
  HelpCommand        _help;
  TermCommand       *_table[3];     // command registry
  TermCommand       *_active;       // cached parsed command (nullptr = idle)
  TermCommand       *_lastCmd;      // last valid command entered, for the V43 re-run button
  char               _args[CMD_MAX];// cached args for _active / _lastCmd
  uint32_t           _lastChangeMs; // when _active was last (re)entered, for the guard

  TermCommand *_find(const char *name) {
    for (TermCommand *c : _table) if (strcmp(c->name(), name) == 0) return c;
    return nullptr;
  }

  // Copy src->dst, trimming leading/trailing whitespace (incl. CR/LF). Bounded by cap.
  static void _copyTrim(char *dst, const char *src, size_t cap) {
    if (!src || cap == 0) { if (cap) dst[0] = '\0'; return; }
    while (*src == ' ' || *src == '\t') src++;
    size_t n = 0;
    while (*src && n < cap - 1) dst[n++] = *src++;
    while (n > 0 && (dst[n-1] == ' ' || dst[n-1] == '\t' || dst[n-1] == '\r' || dst[n-1] == '\n')) n--;
    dst[n] = '\0';
  }
};
