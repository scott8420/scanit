#pragma once
// model/Loop.hpp -- continuous scanning as a small state machine, GTK-free so
// the selftest walks it. Scott: "scan then pause a set number of seconds and
// scan again until I click a stop."
//
//   idle --start()--> scanning --scanned()--> waiting(n) --tick() x n--> scanning ...
//     ^                  |  failed()               |  stop()
//     +------------------+-------------------------+
//
// The Shell drives it: start() with the orb when the plan says continuous,
// scanned() / failed() from the worker's result, tick() once a second while
// waiting (true = start the next scan now), stop() from Stop. The page count
// is what the running caption shows ("Page 12").

namespace scanit::model {

class Loop {
public:
    enum class Phase { Idle, Scanning, Waiting };

    Phase phase()     const { return m_phase; }
    bool  running()   const { return m_phase != Phase::Idle; }
    int   pages()     const { return m_pages; }   // scanned in this run
    int   left()      const { return m_left; }    // seconds until the next scan

    // The orb, in continuous mode. False if already running.
    bool start(int pause_s) {
        if (running()) return false;
        m_pause = pause_s < 1 ? 1 : pause_s;
        m_pages = 0;
        m_left  = 0;
        m_phase = Phase::Scanning;
        return true;
    }
    // A page came back: count it, start the countdown.
    void scanned() {
        if (m_phase != Phase::Scanning) return;
        ++m_pages;
        m_left  = m_pause;
        m_phase = Phase::Waiting;
    }
    // A scan failed or was cancelled: the run ends (no blind retry loop).
    void failed() { m_phase = Phase::Idle; m_left = 0; }
    // One second passed. True when the countdown hit zero: scan now.
    bool tick() {
        if (m_phase != Phase::Waiting) return false;
        if (--m_left > 0) return false;
        m_left  = 0;
        m_phase = Phase::Scanning;
        return true;
    }
    // "Scan now" during the wait: skip the rest of the countdown.
    bool skip() {
        if (m_phase != Phase::Waiting) return false;
        m_left = 0;
        m_phase = Phase::Scanning;
        return true;
    }
    // Stop. True if it was running.
    bool stop() {
        const bool was = running();
        m_phase = Phase::Idle;
        m_left = 0;
        return was;
    }

private:
    Phase m_phase = Phase::Idle;
    int   m_pause = 8;
    int   m_pages = 0;
    int   m_left  = 0;
};

}  // namespace scanit::model
