/**
 * HT306 Home Theater - Channel Mixer & Gain DSP Controls
 * Handles channel faders, dB trims calculation, solo/mute/phase toggles, master volume, and true-peak limiter.
 */
(function () {
  "use strict";

  // --- ENSURE GLOBAL STATE ---
  window.trimsDb = window.trimsDb || [0.0, 0.0, 1.5, 0.0, -1.0, -1.0];
  window.mutes = window.mutes || [false, false, false, false, false, false];
  window.solos = window.solos || [false, false, false, false, false, false];
  window.phases = window.phases || [false, false, false, false, false, false];
  window.isMuted = window.isMuted !== undefined ? window.isMuted : false;

  function initMixer() {
    const channelCards = document.querySelectorAll(".channel-card");

    // --- CHANNEL STRIPS CONTROLS ---
    channelCards.forEach((card, idx) => {
      const fader = card.querySelector(".ch-fader");
      const readout = card.querySelector(".ch-readout");
      const stepDec = card.querySelector(".step-dec");
      const stepInc = card.querySelector(".step-inc");
      const btnMute = card.querySelector(".btn-mute");
      const btnZero = card.querySelector(".btn-zero");
      const btnSolo = card.querySelector(".btn-solo");
      const btnPhase = card.querySelector(".btn-phase");

      function updateGain(val) {
        const num = parseFloat(val);
        if (fader) fader.value = num;
        if (readout) readout.textContent = (num > 0 ? "+" : "") + num.toFixed(1) + " dB";
        window.trimsDb[idx] = num;
        window.sendNativeCommand("set_gain", { trims_db: window.trimsDb, channel: idx, gain_db: num });
        if (typeof window.sendConfigUpdate === "function") {
          window.sendConfigUpdate({ trims_db: window.trimsDb });
        }
      }

      if (fader) fader.addEventListener("input", (e) => updateGain(e.target.value));
      if (stepDec) stepDec.addEventListener("click", () => updateGain(parseFloat(fader.value) - 0.5));
      if (stepInc) stepInc.addEventListener("click", () => updateGain(parseFloat(fader.value) + 0.5));
      if (btnZero) btnZero.addEventListener("click", () => updateGain(0));

      if (btnMute) {
        btnMute.addEventListener("click", () => {
          window.mutes[idx] = !window.mutes[idx];
          if (window.mutes[idx]) {
            btnMute.classList.add("bg-status-error", "text-text-primary");
            btnMute.classList.remove("bg-surface-2", "text-text-secondary");
            const vu = card.querySelector(".vu-bar");
            if (vu) vu.style.opacity = "0.2";
          } else {
            btnMute.classList.remove("bg-status-error", "text-text-primary");
            btnMute.classList.add("bg-surface-2", "text-text-secondary");
            const vu = card.querySelector(".vu-bar");
            if (vu) vu.style.opacity = "1";
          }
          window.sendNativeCommand("set_mutes", { mutes: window.mutes, channel: idx, muted: window.mutes[idx] });
          if (typeof window.sendConfigUpdate === "function") {
            window.sendConfigUpdate({ mutes: window.mutes });
          }
        });
      }

      if (btnSolo) {
        btnSolo.addEventListener("click", () => {
          window.solos[idx] = !window.solos[idx];
          if (window.solos[idx]) {
            btnSolo.classList.add("bg-status-warn", "text-background");
            btnSolo.classList.remove("bg-surface-2", "text-text-secondary");
          } else {
            btnSolo.classList.remove("bg-status-warn", "text-background");
            btnSolo.classList.add("bg-surface-2", "text-text-secondary");
          }
          window.sendNativeCommand("set_solo", { channel: idx, solo: window.solos[idx] });
          if (typeof window.sendConfigUpdate === "function") {
            window.sendConfigUpdate({ solos: window.solos });
          }
        });
      }

      if (btnPhase) {
        btnPhase.addEventListener("click", () => {
          window.phases[idx] = !window.phases[idx];
          if (window.phases[idx]) {
            btnPhase.classList.add("bg-secondary-container", "text-text-primary");
            btnPhase.classList.remove("bg-surface-2", "text-text-secondary");
            btnPhase.textContent = "Ø";
          } else {
            btnPhase.classList.remove("bg-secondary-container", "text-text-primary");
            btnPhase.classList.add("bg-surface-2", "text-text-secondary");
            btnPhase.textContent = "O";
          }
          window.sendNativeCommand("set_phase", { channel: idx, invert: window.phases[idx] });
          if (typeof window.sendConfigUpdate === "function") {
            window.sendConfigUpdate({ phase_inverts: window.phases });
          }
        });
      }
    });

    // --- MASTER VOLUME CONTROLS ---
    function updateMaster(val) {
      const num = parseFloat(val);
      document.querySelectorAll("#master-slider").forEach((s) => {
        if (s !== document.activeElement) s.value = num;
      });
      document.querySelectorAll("#master-vol-num").forEach((v) => {
        v.textContent = (num > 0 ? "+" : "") + num.toFixed(1) + " dB";
      });
      document.querySelectorAll("#master-vol-readout").forEach((v) => {
        v.textContent = (num > 0 ? "+" : "") + num.toFixed(1);
      });
      const badge = document.getElementById("dash-vol-status-badge");
      if (badge) {
        if (num === 0) {
          badge.innerText = "● Unity Gain (100%)";
          badge.className = "text-[11px] font-mono text-status-ok mt-1";
        } else if (num > 0) {
          badge.innerText = "▲ Boosted Level (+dB)";
          badge.className = "text-[11px] font-mono text-status-warn mt-1";
        } else if (num <= -40) {
          badge.innerText = "▼ Whispering Quiet";
          badge.className = "text-[11px] font-mono text-icon-gray mt-1";
        } else {
          badge.innerText = "▼ Attenuated";
          badge.className = "text-[11px] font-mono text-text-secondary mt-1";
        }
      }
      window.sendNativeCommand("set_master_volume", { volume_db: num, muted: window.isMuted });
      if (typeof window.sendConfigUpdate === "function") {
        window.sendConfigUpdate({ master_volume_db: num });
      }
    }
    window.updateMaster = updateMaster;

    function toggleMasterMute() {
      window.isMuted = !window.isMuted;
      document.querySelectorAll("#mute-label").forEach((muteLabel) => {
        muteLabel.textContent = window.isMuted ? "Unmute Master" : "Mute Master";
      });

      document.querySelectorAll("#master-mute-btn, #mute-btn").forEach((btn) => {
        if (window.isMuted) {
          btn.classList.add("bg-status-error", "text-text-primary");
          btn.classList.remove("bg-surface-2", "text-text-secondary");
        } else {
          btn.classList.remove("bg-status-error", "text-text-primary");
          btn.classList.add("bg-surface-2", "text-text-secondary");
        }
      });
      const badge = document.getElementById("dash-vol-status-badge");
      if (badge && window.isMuted) {
        badge.innerText = "✕ Audio Output Dimatikan (Muted)";
        badge.className = "text-[11px] font-mono text-status-error mt-1";
      }

      const s = document.getElementById("master-slider");
      const curVol = s ? parseFloat(s.value) : 0.0;
      window.sendNativeCommand("set_master_volume", { volume_db: curVol, muted: window.isMuted });
      if (typeof window.sendConfigUpdate === "function") {
        window.sendConfigUpdate({ mute_master: window.isMuted });
      }
    }
    window.toggleMasterMute = toggleMasterMute;

    document.querySelectorAll("#master-slider").forEach((s) => {
      s.addEventListener("input", (e) => updateMaster(e.target.value));
    });
    document.querySelectorAll("#master-zero-btn").forEach((b) => {
      b.addEventListener("click", () => updateMaster(0));
    });
    document.querySelectorAll("#master-mute-btn, #mute-btn").forEach((b) => {
      b.addEventListener("click", toggleMasterMute);
    });

    // Explicit window exposure for inline onclick="setMasterDb(val)"
    window.setMasterDb = function (val) {
      updateMaster(val);
    };

    // --- RESET ALL TRIMS TO 0 dB ---
    const resetAllBtn = document.getElementById("reset-all-btn");
    if (resetAllBtn) {
      resetAllBtn.addEventListener("click", () => {
        window.trimsDb = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0];
        channelCards.forEach((c) => {
          const f = c.querySelector(".ch-fader");
          const r = c.querySelector(".ch-readout");
          if (f) f.value = 0;
          if (r) r.textContent = "0.0 dB";
        });
        window.sendNativeCommand("set_gain", { trims_db: window.trimsDb, reset: true });
        if (typeof window.sendConfigUpdate === "function") {
          window.sendConfigUpdate({ trims_db: window.trimsDb });
        }
      });
    }

    // --- RESET PEAKS INDICATORS ---
    const resetPeaksBtn = document.getElementById("reset-peaks-btn");
    if (resetPeaksBtn) {
      resetPeaksBtn.addEventListener("click", () => {
        (window.CHANNEL_LOWER || ["fl", "fr", "c", "lfe", "sl", "sr"]).forEach((ch) => {
          const el = document.getElementById("peak-" + ch);
          if (el) {
            el.style.opacity = "0";
            setTimeout(() => { el.style.opacity = "1"; }, 300);
          }
        });
        window.sendNativeCommand("reset_peaks", {});
      });
    }

    // --- TRUE-PEAK / SOFT-KNEE LIMITER TOGGLE ---
    const limiterToggle = document.getElementById("limiter-toggle");
    if (limiterToggle) {
      limiterToggle.addEventListener("change", (e) => {
        window.sendNativeCommand("set_limiter", { limiter_enabled: e.target.checked });
        if (typeof window.sendConfigUpdate === "function") {
          window.sendConfigUpdate({ limiter_enabled: e.target.checked });
        }
      });
    }
  }

  window.initMixer = initMixer;

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", initMixer);
  } else {
    initMixer();
  }
})();
