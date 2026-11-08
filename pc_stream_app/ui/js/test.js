/**
 * HT306 Home Theater - 5.1 Acoustic Sweep & Speaker Signal Generator Engine
 * Controls pink noise, chirp sweep, 1kHz sine calibration, and speaker channel sequencing.
 */
(function () {
  "use strict";

  const SIGNAL_NAMES = [
    "CHIRP SWEEP (20Hz - 20kHz)",
    "PINK NOISE",
    "1kHz SINE TONE",
    "DUAL-TONE IMD (60Hz + 7kHz)",
    "POLARITY PULSE"
  ];

  window.testEngine = {
    sweepRunning: false,
    activeChannel: "C",
    sweepInterval: null,
    order: ["FL", "C", "FR", "SR", "SL", "LFE"],
    orderIndex: 0,
    levelDb: -20.0,
    signalType: 1, // Default: Pink Noise (1). 0 = Chirp, 2 = 1kHz Sine, 3 = Dual-Tone, 4 = Polarity Pulse

    toggleSweep: function () {
      const btn = document.getElementById("sweep-trigger-btn");
      const statusText = document.getElementById("sweep-status-text");
      this.sweepRunning = !this.sweepRunning;

      if (this.sweepRunning) {
        if (btn) {
          btn.innerHTML = '<span class="material-symbols-outlined text-[18px]">pause</span><span>Pause Sweep</span>';
          btn.classList.add("bg-primary-container");
        }
        if (statusText) statusText.innerText = "AUTO-SWEEP ACTIVE";

        this.runStep();
        this.sweepInterval = setInterval(() => {
          this.orderIndex = (this.orderIndex + 1) % this.order.length;
          this.runStep();
        }, 1500);
      } else {
        clearInterval(this.sweepInterval);
        this.stopSweepApi();
        if (btn) {
          btn.innerHTML = '<span class="material-symbols-outlined text-[18px]">graphic_eq</span><span>Start 5.1 Auto-Sweep</span>';
          btn.classList.remove("bg-primary-container");
        }
        if (statusText) statusText.innerText = "SWEEP PAUSED";
      }
    },

    runStep: function () {
      const ch = this.order[this.orderIndex];
      this.setActiveSpeaker(ch);
      this.sendSweepApi("start", ch, this.levelDb);
    },

    setActiveSpeaker: function (chId) {
      this.activeChannel = chId;
      const allNodes = document.querySelectorAll("[data-speaker-node]");
      allNodes.forEach((el) => {
        const isTarget = el.getAttribute("data-speaker-node") === chId;
        const pingRing = el.querySelector(".speaker-ping-ring");
        const statusBadge = el.querySelector(".speaker-active-tag");
        if (isTarget) {
          el.classList.add("bg-surface-2", "ring-2", "ring-primary-container");
          if (pingRing) pingRing.classList.remove("opacity-0");
          if (statusBadge) statusBadge.classList.remove("hidden");
        } else {
          el.classList.remove("bg-surface-2", "ring-2", "ring-primary-container");
          if (pingRing) pingRing.classList.add("opacity-0");
          if (statusBadge) statusBadge.classList.add("hidden");
        }
      });

      const liveReadout = document.getElementById("current-channel-readout");
      if (liveReadout) liveReadout.innerText = chId;

      const statusText = document.getElementById("sweep-status-text");
      if (statusText && this.sweepRunning) {
        const sigName = SIGNAL_NAMES[this.signalType] || "AUDIO TEST TONE";
        statusText.innerText = `CHANNEL (${chId}) PLAYING ${sigName}`;
      }
    },

    setSignalType: function (typeIndex) {
      this.signalType = Math.max(0, Math.min(SIGNAL_NAMES.length - 1, typeIndex));
      const sigName = SIGNAL_NAMES[this.signalType] || "AUDIO TEST TONE";

      const statusText = document.getElementById("sweep-status-text");
      if (statusText) {
        statusText.innerText = this.sweepRunning
          ? `CHANNEL (${this.activeChannel}) PLAYING ${sigName}`
          : `GENERATOR ARMED: ${sigName}`;
      }

      // Update button visual styles
      const patternBtns = document.querySelectorAll(".flex-wrap > button");
      patternBtns.forEach((btn, idx) => {
        if (idx === this.signalType) {
          btn.className = "px-space-md py-1.5 rounded-full bg-primary-container text-text-primary font-code-sm text-code-sm font-medium shadow-sm cursor-pointer";
        } else if (idx < SIGNAL_NAMES.length) {
          btn.className = "px-space-md py-1.5 rounded-full bg-surface-2 hover:bg-surface-container-high text-text-secondary hover:text-text-primary font-code-sm text-code-sm transition-colors cursor-pointer";
        }
      });

      if (this.sweepRunning) {
        this.sendSweepApi("start", this.activeChannel, this.levelDb);
      }
    },

    adjustLevel: function (delta) {
      this.levelDb = Math.min(-12, Math.max(-60, this.levelDb + delta));
      const meter = document.getElementById("dbfs-val");
      if (meter) meter.innerText = this.levelDb.toFixed(1) + " dBFS";
      if (this.sweepRunning) {
        this.sendSweepApi("start", this.activeChannel, this.levelDb);
      }
    },

    sendSweepApi: function (action, channel, levelDb) {
      window.sendNativeCommand("sweep_test", {
        sweep_action: action,
        channel: channel,
        level_db: levelDb,
        signal_type: this.signalType
      });

      if (!window.isNativeWebView2 && window.location.protocol.startsWith("http")) {
        fetch("/api/test_sweep", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ action: action, channel: channel, level_db: levelDb, signal_type: this.signalType })
        }).catch((err) => console.warn("[HT306 Test] Sweep API error:", err));
      }
    },

    stopSweepApi: function () {
      this.sendSweepApi("stop", -1, this.levelDb);
    }
  };

  function initTest() {
    // Sweep trigger button
    const sweepTriggerBtn = document.getElementById("sweep-trigger-btn");
    if (sweepTriggerBtn) {
      sweepTriggerBtn.removeAttribute("onclick");
      sweepTriggerBtn.addEventListener("click", () => {
        window.testEngine.toggleSweep();
      });
    }

    // Sweep abort / stop button
    const sweepAbortBtn = document.querySelector("[onclick*='sweepRunning = true']");
    if (sweepAbortBtn) {
      sweepAbortBtn.removeAttribute("onclick");
      sweepAbortBtn.addEventListener("click", () => {
        if (window.testEngine.sweepRunning) {
          window.testEngine.toggleSweep();
        } else {
          window.testEngine.stopSweepApi();
        }
      });
    }

    // Interactive Speaker Diagram click handlers
    document.querySelectorAll("[data-speaker-node]").forEach((node) => {
      const ch = node.getAttribute("data-speaker-node");
      node.addEventListener("click", () => {
        window.testEngine.setActiveSpeaker(ch);
        window.testEngine.sendSweepApi("start", ch, window.testEngine.levelDb);
      });
    });

    // Signal Pattern Selector Chips
    const patternContainer = document.querySelector(".flex-wrap");
    if (patternContainer) {
      const patternBtns = patternContainer.querySelectorAll("button");
      patternBtns.forEach((btn, idx) => {
        if (idx < SIGNAL_NAMES.length) {
          btn.addEventListener("click", () => {
            window.testEngine.setSignalType(idx);
          });
        }
      });
    }
  }

  window.initTest = initTest;

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", initTest);
  } else {
    initTest();
  }
})();
