/**
 * HT306 Home Theater - Native Telemetry Processor & VU Meter Updates
 * Receives real-time telemetry from C++ DSP pipeline and updates UI meters, hardware badges, and states.
 */
(function () {
  "use strict";

  // --- GLOBAL STATE INITIALIZERS ---
  window.CHANNEL_NAMES = window.CHANNEL_NAMES || ["FL", "FR", "C", "LFE", "SL", "SR"];
  window.CHANNEL_LOWER = window.CHANNEL_LOWER || ["fl", "fr", "c", "lfe", "sl", "sr"];
  window.trimsDb = window.trimsDb || [0.0, 0.0, 1.5, 0.0, -1.0, -1.0];
  window.mutes = window.mutes || [false, false, false, false, false, false];
  window.solos = window.solos || [false, false, false, false, false, false];
  window.phases = window.phases || [false, false, false, false, false, false];
  window.routeState = window.routeState || { 0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5 };
  window.logBuffer = window.logBuffer || [];
  window.audioDriverState = window.audioDriverState || { installed: false, is_default: false, name: "", debug_override: false };
  window.isStreaming = window.isStreaming !== undefined ? window.isStreaming : true;
  window.isMuted = window.isMuted !== undefined ? window.isMuted : false;
  window.currentMode = window.currentMode || "desktop";
  window.currentProfile = window.currentProfile || "Movie";
  window.currentTab = window.currentTab || "dashboard";
  window.appUptimeSeconds = window.appUptimeSeconds || 0;

  // --- UTILITY FUNCTIONS ---
  function dbfsToPercent(db) {
    if (db <= -60) return 0;
    if (db >= 0) return 100;
    return ((db + 60) / 60) * 100;
  }
  window.dbfsToPercent = dbfsToPercent;

  function formatUptime(seconds) {
    const hrs = Math.floor(seconds / 3600).toString().padStart(2, "0");
    const mins = Math.floor((seconds % 3600) / 60).toString().padStart(2, "0");
    const secs = Math.floor(seconds % 60).toString().padStart(2, "0");
    return `${hrs}:${mins}:${secs}`;
  }
  window.formatUptime = formatUptime;

  function escapeHtml(str) {
    return String(str).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  }
  window.escapeHtml = escapeHtml;

  // --- UI STATUS HELPERS ---
  function updateStreamButtonUi(streaming) {
    const streamBtn = document.getElementById("toggle-stream-btn");
    const dashDot = document.getElementById("dash-connected-dot");
    const dashText = document.getElementById("dash-connected-text");
    const headerDot = document.getElementById("header-status-dot");
    const headerText = document.getElementById("header-status-text");

    if (streamBtn) {
      if (streaming) {
        streamBtn.innerHTML = '<span class="material-symbols-outlined text-[18px]">stop_circle</span><span>Stop Stream</span>';
        streamBtn.className = "px-space-xl py-2.5 rounded-full bg-primary-container hover:bg-lg-red-hover active:bg-lg-red-pressed text-text-primary font-body-sm text-body-sm font-semibold shadow-lg shadow-primary-container/20 transition-all flex items-center justify-center gap-2 cursor-pointer";
      } else {
        streamBtn.innerHTML = '<span class="material-symbols-outlined text-[18px]">play_circle</span><span>Start Stream</span>';
        streamBtn.className = "px-space-xl py-2.5 rounded-full bg-status-ok hover:opacity-90 active:opacity-80 text-background font-body-sm text-body-sm font-semibold shadow-lg transition-all flex items-center justify-center gap-2 cursor-pointer";
      }
    }
    if (dashDot && dashText) {
      dashDot.className = "w-2.5 h-2.5 rounded-full bg-status-ok";
      dashText.innerText = streaming ? "● STREAMING" : "● CONNECTED";
    }
    if (headerDot && headerText) {
      headerDot.className = "w-2 h-2 rounded-full bg-status-ok";
      headerText.innerText = streaming ? "Streaming" : "Connected";
    }
  }
  window.updateStreamButtonUi = updateStreamButtonUi;

  function updateModeUi(mode) {
    const desktopBtn = document.getElementById("mode-desktop-btn");
    const passBtn = document.getElementById("mode-passthrough-btn");
    const bypassPill = document.getElementById("dsp-bypass-pill");

    if (mode === "bitperfect") {
      if (passBtn) passBtn.className = "px-space-md py-1 rounded-full font-code-sm text-code-sm font-medium transition-all bg-primary-container text-text-primary shadow-sm cursor-pointer";
      if (desktopBtn) desktopBtn.className = "px-space-md py-1 rounded-full font-code-sm text-code-sm text-text-secondary hover:text-text-primary transition-all cursor-pointer";
      if (bypassPill) {
        bypassPill.classList.remove("hidden");
        bypassPill.classList.add("flex");
      }
    } else {
      if (desktopBtn) desktopBtn.className = "px-space-md py-1 rounded-full font-code-sm text-code-sm font-medium transition-all bg-primary-container text-text-primary shadow-sm cursor-pointer";
      if (passBtn) passBtn.className = "px-space-md py-1 rounded-full font-code-sm text-code-sm text-text-secondary hover:text-text-primary transition-all cursor-pointer";
      if (bypassPill) {
        bypassPill.classList.add("hidden");
        bypassPill.classList.remove("flex");
      }
    }
  }
  window.updateModeUi = updateModeUi;

  // --- NATIVE TELEMETRY PROCESSOR ---
  function handleNativeTelemetry(raw) {
    let data = raw;
    if (typeof raw === "string") {
      try {
        data = JSON.parse(raw);
      } catch (e) {
        console.warn("[HT306 Telemetry] Parse error:", e);
        return;
      }
    }
    if (!data || typeof data !== "object") return;

    // 1. Connection & Link Status
    if (data.state !== undefined || data.is_streaming !== undefined || data.connected !== undefined) {
      let streaming = false;
      let connected = false;

      if (data.state === "STREAMING" || data.state === 3 || data.is_streaming === true) {
        streaming = true;
        connected = true;
      } else if (data.state === "READY" || data.state === "CONNECTED" || data.state === 2 || data.connected === true) {
        connected = true;
      }

      window.isStreaming = streaming;
      updateStreamButtonUi(streaming);

      const headerDot = document.getElementById("header-status-dot");
      const headerText = document.getElementById("header-status-text");
      const dashDot = document.getElementById("dash-connected-dot");
      const dashText = document.getElementById("dash-connected-text");

      if (streaming) {
        if (headerDot) headerDot.className = "w-2 h-2 rounded-full bg-status-ok";
        if (headerText) {
          headerText.innerText = "Streaming";
          headerText.className = "font-code-sm text-code-sm text-status-ok font-medium";
        }
        if (dashDot) dashDot.className = "w-2.5 h-2.5 rounded-full bg-status-ok";
        if (dashText) dashText.innerText = "● STREAMING";
      } else if (connected) {
        if (headerDot) headerDot.className = "w-2 h-2 rounded-full bg-status-ok";
        if (headerText) {
          headerText.innerText = "Connected";
          headerText.className = "font-code-sm text-code-sm text-status-ok font-medium";
        }
        if (dashDot) dashDot.className = "w-2.5 h-2.5 rounded-full bg-status-ok";
        if (dashText) dashText.innerText = "● CONNECTED";
      } else {
        if (headerDot) headerDot.className = "w-2 h-2 rounded-full bg-status-error";
        if (headerText) {
          headerText.innerText = "Disconnected";
          headerText.className = "font-code-sm text-code-sm text-status-error font-medium";
        }
        if (dashDot) dashDot.className = "w-2.5 h-2.5 rounded-full bg-status-error";
        if (dashText) dashText.innerText = "● DISCONNECTED";
      }
    }

    // 2. Format: 96kHz / 32b
    const rate = (data.format && data.format.rate) ||
                 (data.telemetry && data.telemetry.sample_rate) ||
                 data.sample_rate || 96000;
    const bits = (data.format && data.format.bits) ||
                 (data.telemetry && data.telemetry.bit_depth) ||
                 data.bit_depth || 32;
    const channels = (data.format && data.format.channels) ||
                     (data.telemetry && data.telemetry.channels) ||
                     data.channels || 6;

    const asideFmt = document.getElementById("aside-format-badge");
    if (asideFmt) asideFmt.innerText = `${Math.round(rate / 1000)}kHz / ${bits}b`;

    const dashRate = document.getElementById("dash-sample-rate");
    if (dashRate) dashRate.innerText = `${(rate / 1000).toFixed(1)} kHz`;

    const dashBits = document.getElementById("dash-bit-depth");
    if (dashBits) dashBits.innerText = `${bits}-bit Container`;

    const dashCh = document.getElementById("dash-channels");
    if (dashCh) dashCh.innerText = channels === 6 ? "5.1 Surround" : `${channels} Channels`;

    // 3. Device & COM port
    const comPort = (data.device && data.device.com_port) ||
                    (data.hw_info && data.hw_info.com_port) ||
                    data.com_port || null;
    if (comPort) {
      const asideCom = document.getElementById("aside-com-port");
      if (asideCom) asideCom.innerText = comPort;
      const dashCom = document.getElementById("dash-com-port");
      if (dashCom) dashCom.innerText = comPort;
    }

    const vid = (data.device && data.device.vid) || (data.hw_info && data.hw_info.vid) || data.vid;
    const pid = (data.device && data.device.pid) || (data.hw_info && data.hw_info.pid) || data.pid;
    if (vid !== undefined && pid !== undefined) {
      const dashVidPid = document.getElementById("dash-vid-pid");
      if (dashVidPid) {
        const vidHex = typeof vid === "number" ? "0x" + vid.toString(16).toUpperCase() : vid;
        const pidHex = typeof pid === "number" ? "0x" + pid.toString(16).toUpperCase() : pid;
        dashVidPid.innerText = `${vidHex} : ${pidHex}`;
      }
    }

    // 4. Uptime
    const uptimeSec = (data.hw_info && data.hw_info.uptime_sec !== undefined) ? data.hw_info.uptime_sec :
                      (data.uptime !== undefined ? data.uptime : null);
    if (uptimeSec !== null) {
      window.appUptimeSeconds = uptimeSec;
      const dashUptime = document.getElementById("dash-uptime");
      if (dashUptime) dashUptime.innerText = formatUptime(uptimeSec);
    }

    // 5. FIFO Depth & Buffer / Telemetry
    const fifoDepth = data.fifo_depth || (data.telemetry && data.telemetry.fifo_depth) || 1024;
    const fifoElem = document.getElementById("dash-fifo-depth");
    if (fifoElem) {
      fifoElem.innerText = `FIFO DEPTH: ${fifoDepth} BYTES │ LATENCY OFFSET: 0.00ms`;
    }

    const bufferFill = (data.telemetry && data.telemetry.buffer_fill_pct !== undefined) ? data.telemetry.buffer_fill_pct :
                       (data.buffer_fill_pct !== undefined ? data.buffer_fill_pct : null);
    if (bufferFill !== null) {
      const dashBuf = document.getElementById("dash-buffer-fill");
      if (dashBuf) dashBuf.innerText = `${Math.round(bufferFill)}% Fill`;
    }

    const linkPct = (data.telemetry && data.telemetry.link_usage_pct !== undefined) ? data.telemetry.link_usage_pct :
                    (data.link_usage_pct !== undefined ? data.link_usage_pct : null);
    if (linkPct !== null) {
      const dashLinkPct = document.getElementById("dash-link-pct");
      if (dashLinkPct) dashLinkPct.innerText = `${linkPct}% Utilized`;
      const dashLinkBar = document.getElementById("dash-link-bar");
      if (dashLinkBar) dashLinkBar.style.width = `${linkPct}%`;
    }

    const throughput = (data.telemetry && data.telemetry.throughput_kbs !== undefined) ? data.telemetry.throughput_kbs :
                       (data.throughput_kbs !== undefined ? data.throughput_kbs : null);
    if (throughput !== null) {
      const dashBitrate = document.getElementById("dash-bitrate");
      if (dashBitrate) dashBitrate.innerText = `${Math.round(throughput)} KB/s Current Rate`;
    }

    const underruns = (data.telemetry && data.telemetry.underrun_count !== undefined) ? data.telemetry.underrun_count :
                      (data.stats && data.stats.dropped !== undefined) ? data.stats.dropped :
                      (data.underruns !== undefined ? data.underruns : null);
    if (underruns !== null) {
      const dashUnderruns = document.getElementById("dash-underruns");
      if (dashUnderruns) dashUnderruns.innerText = `${underruns} / hr`;
    }

    // 6. VU Meter levels: FL, FR, C, LFE, SL, SR
    let dBFS = null;
    let peakHolds = null;

    if (Array.isArray(data.dBFS)) {
      dBFS = data.dBFS;
      peakHolds = Array.isArray(data.peak_hold_db) ? data.peak_hold_db : data.dBFS;
    } else if (Array.isArray(data.meters)) {
      dBFS = [];
      peakHolds = [];
      for (let i = 0; i < data.meters.length; i++) {
        const m = data.meters[i];
        if (typeof m === "number") {
          dBFS.push(m);
          peakHolds.push(m);
        } else if (m && typeof m === "object") {
          dBFS.push(m.peak_db !== undefined ? m.peak_db : (m.db !== undefined ? m.db : -60.0));
          peakHolds.push(m.peak_hold_db !== undefined ? m.peak_hold_db : dBFS[i]);
        }
      }
    }

    if (dBFS && dBFS.length >= 6) {
      let maxDb = -96;
      let clipDetected = false;

      for (let i = 0; i < window.CHANNEL_NAMES.length; i++) {
        const chUpper = window.CHANNEL_NAMES[i];
        const chLower = window.CHANNEL_LOWER[i];
        const db = dBFS[i];
        const hold = (peakHolds && peakHolds[i] !== undefined) ? peakHolds[i] : db;

        if (db > maxDb) maxDb = db;
        if (db >= -0.1) clipDetected = true;

        const pct = dbfsToPercent(db);
        const holdPct = dbfsToPercent(hold);

        // Dashboard horizontal bars
        const bar = document.getElementById("meter-bar-" + chLower);
        const peakMarker = document.getElementById("peak-" + chLower);
        const valText = document.getElementById("val-" + chLower);

        if (bar) bar.style.width = pct.toFixed(1) + "%";
        if (peakMarker) peakMarker.style.left = holdPct.toFixed(1) + "%";
        if (valText) {
          valText.innerText = db > -80 ? db.toFixed(1) + " dB" : "-∞ dB";
          if (db >= -3.0) {
            valText.className = "w-16 text-right font-code-sm text-code-sm text-status-warn font-semibold";
          } else {
            valText.className = "w-16 text-right font-code-sm text-code-sm text-text-primary font-medium";
          }
        }

        // Mixer vertical VU bars
        const mixerCard = document.querySelector(`.channel-card[data-ch="${chUpper}"]`) ||
                          document.querySelector(`.channel-card[data-ch="${chUpper === 'LFE' ? 'SW' : chUpper}"]`);
        if (mixerCard) {
          const mixerVu = mixerCard.querySelector(".vu-bar");
          if (mixerVu && !window.mutes[i]) {
            mixerVu.style.height = pct.toFixed(1) + "%";
          }
        }
      }

      // Clip indicator
      const clipDot = document.getElementById("dash-clip-dot");
      const clipText = document.getElementById("dash-clip-text");
      if (clipDot && clipText) {
        if (clipDetected) {
          clipDot.className = "w-2.5 h-2.5 rounded-full bg-status-error";
          clipText.className = "font-code-sm text-code-sm text-status-error font-bold";
          clipText.innerText = "CLIP DETECTED: 0 dBFS";
        } else {
          clipDot.className = "w-2.5 h-2.5 rounded-full bg-status-ok";
          clipText.className = "font-code-sm text-code-sm text-status-ok font-medium";
          clipText.innerText = "CLIP: NONE (Clean Headroom)";
        }
      }

      // Master Meter Bar in Mixer
      const masterMeterBar = document.getElementById("master-meter-bar");
      if (masterMeterBar) {
        masterMeterBar.style.width = dbfsToPercent(maxDb).toFixed(1) + "%";
      }
    }

    // 7. Hardware flags & Heartbeat
    const heartbeat = data.heartbeat_text || (data.hw_info && data.hw_info.heartbeat_text);
    if (heartbeat) {
      const hbElem = document.getElementById("dash-heartbeat");
      if (hbElem) hbElem.innerText = heartbeat;
    }

    const pioReady = data.pio_ready !== undefined ? data.pio_ready : (data.hw_info && data.hw_info.pio_ready);
    if (pioReady !== undefined) {
      const pioElem = document.getElementById("dash-pio-status");
      if (pioElem) {
        pioElem.innerText = pioReady ? "READY" : "OFFLINE";
        pioElem.className = pioReady ? "font-code-lg text-code-lg text-tertiary font-semibold" : "font-code-lg text-code-lg text-status-error font-semibold";
      }
    }

    const muxHigh = data.mux_high !== undefined ? data.mux_high : (data.hw_info && data.hw_info.mux_high);
    if (muxHigh !== undefined) {
      const muxElem = document.getElementById("dash-mux-status");
      if (muxElem) {
        muxElem.innerText = muxHigh ? "HIGH" : "LOW";
        muxElem.className = muxHigh ? "font-code-lg text-code-lg text-status-info font-semibold" : "font-code-lg text-code-lg text-status-warn font-semibold";
      }
    }

    const dspLocked = data.dsp_locked !== undefined ? data.dsp_locked : (data.hw_info && data.hw_info.dsp_locked);
    if (dspLocked !== undefined) {
      const dspElem = document.getElementById("dash-dsp-status");
      if (dspElem) {
        dspElem.innerText = dspLocked ? "LOCKED" : "UNLOCKED";
        dspElem.className = dspLocked ? "font-code-lg text-code-lg text-text-primary font-semibold" : "font-code-lg text-code-lg text-status-warn font-semibold";
      }
    }

    // 8. Format Mode
    if (data.format_mode !== undefined || (data.config && data.config.format_mode !== undefined)) {
      const fmode = data.format_mode !== undefined ? data.format_mode : data.config.format_mode;
      if (typeof window.setFormatModeUi === "function") {
        window.setFormatModeUi(fmode);
      }
    }

    // 9. Master Volume & Master Mute
    if (data.master_volume_db !== undefined || (data.config && data.config.master_volume_db !== undefined)) {
      const vol = data.master_volume_db !== undefined ? data.master_volume_db : data.config.master_volume_db;
      const mSlider = document.getElementById("master-slider");
      const mVolNum = document.getElementById("master-vol-num");
      if (mSlider && document.activeElement !== mSlider) {
        mSlider.value = vol;
      }
      if (mVolNum) {
        mVolNum.textContent = (vol > 0 ? "+" : "") + vol.toFixed(1) + " dB";
      }
    }

    if (data.mute_master !== undefined || (data.config && data.config.mute_master !== undefined)) {
      const mm = data.mute_master !== undefined ? data.mute_master : data.config.mute_master;
      window.isMuted = mm;
      const muteLabel = document.getElementById("mute-label");
      if (muteLabel) muteLabel.textContent = window.isMuted ? "Unmute Master" : "Mute Master";
      const mMuteBtn = document.getElementById("master-mute-btn");
      if (mMuteBtn) {
        if (window.isMuted) {
          mMuteBtn.classList.add("bg-status-error", "text-text-primary");
          mMuteBtn.classList.remove("bg-surface-2", "text-text-secondary");
        } else {
          mMuteBtn.classList.remove("bg-status-error", "text-text-primary");
          mMuteBtn.classList.add("bg-surface-2", "text-text-secondary");
        }
      }
    }

    // 10. Solos and Phase inverts sync
    const solosArr = data.solos || (data.config && data.config.solos);
    const channelCards = document.querySelectorAll(".channel-card");
    if (Array.isArray(solosArr) && solosArr.length >= 6) {
      for (let i = 0; i < 6; i++) {
        if (window.solos[i] !== solosArr[i]) {
          window.solos[i] = solosArr[i];
          const card = channelCards[i];
          if (card) {
            const bSolo = card.querySelector(".btn-solo");
            if (bSolo) {
              if (window.solos[i]) {
                bSolo.classList.add("bg-status-warn", "text-background");
                bSolo.classList.remove("bg-surface-2", "text-text-secondary");
              } else {
                bSolo.classList.remove("bg-status-warn", "text-background");
                bSolo.classList.add("bg-surface-2", "text-text-secondary");
              }
            }
          }
        }
      }
    }

    const phasesArr = data.phase_inverts || data.phases || (data.config && (data.config.phase_inverts || data.config.phases));
    if (Array.isArray(phasesArr) && phasesArr.length >= 6) {
      for (let i = 0; i < 6; i++) {
        if (window.phases[i] !== phasesArr[i]) {
          window.phases[i] = phasesArr[i];
          const card = channelCards[i];
          if (card) {
            const bPhase = card.querySelector(".btn-phase");
            if (bPhase) {
              if (window.phases[i]) {
                bPhase.classList.add("bg-secondary-container", "text-text-primary");
                bPhase.classList.remove("bg-surface-2", "text-text-secondary");
                bPhase.textContent = "Ø";
              } else {
                bPhase.classList.remove("bg-secondary-container", "text-text-primary");
                bPhase.classList.add("bg-surface-2", "text-text-secondary");
                bPhase.textContent = "O";
              }
            }
          }
        }
      }
    }

    // 11. Channel mutes sync
    const mutesArr = data.mutes || (data.config && data.config.mutes);
    if (Array.isArray(mutesArr) && mutesArr.length >= 6) {
      for (let i = 0; i < 6; i++) {
        if (window.mutes[i] !== mutesArr[i]) {
          window.mutes[i] = mutesArr[i];
          const chUpper = window.CHANNEL_NAMES[i];
          const mixerCard = document.querySelector(`.channel-card[data-ch="${chUpper}"]`) ||
                            document.querySelector(`.channel-card[data-ch="${chUpper === 'LFE' ? 'SW' : chUpper}"]`);
          if (mixerCard) {
            const btnMute = mixerCard.querySelector(".btn-mute");
            const vu = mixerCard.querySelector(".vu-bar");
            if (btnMute) {
              if (window.mutes[i]) {
                btnMute.classList.add("bg-status-error", "text-text-primary");
                btnMute.classList.remove("bg-surface-2", "text-text-secondary");
                if (vu) vu.style.opacity = "0.2";
              } else {
                btnMute.classList.remove("bg-status-error", "text-text-primary");
                btnMute.classList.add("bg-surface-2", "text-text-secondary");
                if (vu) vu.style.opacity = "1";
              }
            }
          }
        }
      }
    }

    // 12. Routing map sync
    const rmap = data.routing_map || (data.config && data.config.routing_map);
    if (Array.isArray(rmap) && rmap.length >= 6) {
      let rChanged = false;
      for (let i = 0; i < 6; i++) {
        if (window.routeState[i] !== rmap[i]) {
          window.routeState[i] = rmap[i];
          rChanged = true;
        }
      }
      if (rChanged && typeof window.renderMatrix === "function") {
        window.renderMatrix();
      }
    }

    // 13. Audio Capture Devices sync
    if (Array.isArray(data.devices) && data.devices.length > 0) {
      const devSelect = document.getElementById("setting-device-select");
      if (devSelect) {
        let needsUpdate = devSelect.options.length !== data.devices.length;
        if (!needsUpdate) {
          for (let i = 0; i < data.devices.length; i++) {
            const expectedText = data.devices[i].name + (data.devices[i].is_default ? " (Default)" : "");
            if (devSelect.options[i].text !== expectedText) {
              needsUpdate = true;
              break;
            }
          }
        }
        if (needsUpdate) {
          devSelect.innerHTML = "";
          data.devices.forEach((dev) => {
            const opt = document.createElement("option");
            opt.value = dev.index;
            opt.textContent = dev.name + (dev.is_default ? " (Default)" : "");
            devSelect.appendChild(opt);
          });
        }
        const activeIdx = data.active_device_index !== undefined ? data.active_device_index :
                          (data.config && data.config.active_device_index !== undefined ? data.config.active_device_index : 0);
        if (String(devSelect.value) !== String(activeIdx)) {
          devSelect.value = activeIdx;
        }
      }

      const dashDevName = document.getElementById("dash-device-name");
      if (dashDevName) {
        const activeIdx = data.active_device_index !== undefined ? data.active_device_index : 0;
        const activeDev = data.devices.find(d => d.index === activeIdx) || data.devices[0];
        if (activeDev && dashDevName.textContent !== activeDev.name) {
          dashDevName.textContent = activeDev.name;
        }
      }
    }

    // 14. Limiter & LipSync sync
    if (data.limiter_enabled !== undefined || (data.config && data.config.limiter_enabled !== undefined)) {
      const lim = data.limiter_enabled !== undefined ? data.limiter_enabled : data.config.limiter_enabled;
      const limiterToggle = document.getElementById("limiter-toggle");
      if (limiterToggle && document.activeElement !== limiterToggle && limiterToggle.checked !== lim) {
        limiterToggle.checked = lim;
      }
    }

    if (data.lip_sync_delay_ms !== undefined || (data.config && data.config.lip_sync_delay_ms !== undefined)) {
      const delay = data.lip_sync_delay_ms !== undefined ? data.lip_sync_delay_ms : data.config.lip_sync_delay_ms;
      const lSlider = document.getElementById("lipsync-slider");
      if (lSlider && document.activeElement !== lSlider && parseInt(lSlider.value, 10) !== delay) {
        if (typeof window.updateDelayUi === "function") {
          window.updateDelayUi(delay);
        }
      }
    }

    // 15. Config / Profiles sync
    if (data.profile || (data.config && data.config.profile) || (data.settings && data.settings.active_profile)) {
      const prof = data.profile || (data.config && data.config.profile) || data.settings.active_profile;
      if (window.currentProfile !== prof) {
        window.currentProfile = prof;
        const headerProf = document.getElementById("header-profile-text");
        if (headerProf) headerProf.innerText = prof;
      }
    }

    if (data.mode || (data.config && data.config.mode)) {
      const m = data.mode || data.config.mode;
      if (m !== window.currentMode) {
        window.currentMode = m;
        updateModeUi(window.currentMode);
      }
    }

    if (data.trims_db || (data.config && data.config.trims_db)) {
      const t = data.trims_db || data.config.trims_db;
      if (Array.isArray(t)) window.trimsDb = t;
    }

    // 16. Logs stream handling
    if (Array.isArray(data.logs)) {
      const nextLen = data.logs.length;
      const lastNext = nextLen > 0 ? String(data.logs[nextLen - 1]) : "";
      const lastCur = window.logBuffer.length > 0 ? String(window.logBuffer[window.logBuffer.length - 1]) : "";
      if (nextLen !== window.logBuffer.length || lastNext !== lastCur) {
        window.logBuffer = data.logs.map(l => typeof l === "string" ? l : (l.message || JSON.stringify(l)));
        if (typeof window.renderLogs === "function") window.renderLogs();
      }
    } else if (data.log) {
      const line = typeof data.log === "string" ? data.log : (data.log.message || JSON.stringify(data.log));
      window.logBuffer.push(line);
      if (window.logBuffer.length > 500) window.logBuffer.shift();
      if (typeof window.renderLogs === "function") window.renderLogs();
    }

    // 17. Audio Driver & Windows Taskbar Status sync
    if (data.audio_driver) {
      window.audioDriverState = data.audio_driver;
      const audioDot = document.getElementById("header-audio-dot");
      const audioText = document.getElementById("header-audio-text");
      const settingDriverStatus = document.getElementById("setting-driver-status");
      const btnInstallDriver = document.getElementById("btn-install-vbcable");
      const btnDebugAudio = document.getElementById("btn-debug-audio");
      const dashDebugAudioText = document.getElementById("dash-debug-audio-text");
      const dashDebugAudioIcon = document.getElementById("dash-debug-audio-icon");
      const settingDebugToggle = document.getElementById("setting-debug-audio-toggle");
      const settingDebugBadge = document.getElementById("setting-debug-audio-badge");

      const isDebugOverride = !!window.audioDriverState.debug_override;
      if (settingDebugToggle && document.activeElement !== settingDebugToggle) {
        settingDebugToggle.checked = isDebugOverride;
      }
      if (settingDebugBadge) {
        settingDebugBadge.innerText = isDebugOverride ? "ON (Tanpa RP2350)" : "OFF";
        settingDebugBadge.className = isDebugOverride
          ? "font-code-sm text-[10px] text-status-warn font-semibold px-1.5 py-0.2 rounded bg-status-warn/10 border border-status-warn/40"
          : "font-code-sm text-[10px] text-icon-gray px-1.5 py-0.2 rounded bg-surface border border-border";
      }
      if (btnDebugAudio && dashDebugAudioText) {
        if (isDebugOverride) {
          btnDebugAudio.className = "flex items-center gap-1.5 px-space-sm py-1 rounded bg-status-warn/15 hover:bg-status-warn/25 border border-status-warn/50 text-status-warn font-code-sm text-code-sm font-semibold transition-colors cursor-pointer";
          dashDebugAudioText.innerText = "Debug Audio: ON (Tanpa RP2350)";
          if (dashDebugAudioIcon) dashDebugAudioIcon.className = "material-symbols-outlined text-[15px] text-status-warn";
        } else {
          btnDebugAudio.className = "flex items-center gap-1.5 px-space-sm py-1 rounded bg-surface-2 hover:bg-surface-variant border border-border text-text-secondary hover:text-text-primary font-code-sm text-code-sm transition-colors cursor-pointer";
          dashDebugAudioText.innerText = "Debug Audio: OFF";
          if (dashDebugAudioIcon) dashDebugAudioIcon.className = "material-symbols-outlined text-[15px] text-icon-gray";
        }
      }

      if (window.audioDriverState.installed) {
        const isVisible = window.audioDriverState.visible !== false;
        if (!isVisible) {
          if (audioDot) audioDot.className = "w-2 h-2 rounded-full bg-status-info";
          if (audioText) {
            audioText.innerText = "Taskbar: Standby (RP2350 Off)";
            audioText.className = "font-code-sm text-code-sm text-status-info font-medium";
          }
          if (settingDriverStatus) {
            settingDriverStatus.innerHTML = `<span class="text-status-info font-medium">Terpasang — Siaga (Muncul Saat RP2350 Terhubung)</span>`;
          }
          if (btnInstallDriver) {
            btnInstallDriver.innerHTML = `<span class="material-symbols-outlined text-[16px]">visibility</span><span>Tampilkan Manual (Debug)</span>`;
            btnInstallDriver.className = "px-space-md py-1.5 bg-surface border border-border hover:bg-surface-variant text-text-primary rounded-lg font-caption text-caption font-semibold flex items-center gap-1.5 transition-colors cursor-pointer";
          }
        } else {
          if (window.audioDriverState.is_default) {
            if (audioDot) audioDot.className = isDebugOverride ? "w-2 h-2 rounded-full bg-status-warn" : "w-2 h-2 rounded-full bg-status-ok";
            if (audioText) {
              audioText.innerText = "Taskbar: " + (window.audioDriverState.name || "LG HT306") + (isDebugOverride ? " [DEBUG]" : "");
              audioText.className = isDebugOverride
                ? "font-code-sm text-code-sm text-status-warn font-medium"
                : "font-code-sm text-code-sm text-status-ok font-medium";
            }
          } else {
            if (audioDot) audioDot.className = "w-2 h-2 rounded-full bg-status-info";
            if (audioText) {
              audioText.innerText = "Taskbar: Standby" + (isDebugOverride ? " [DEBUG]" : "");
              audioText.className = "font-code-sm text-code-sm text-status-info font-medium";
            }
          }
          if (settingDriverStatus) {
            settingDriverStatus.innerHTML = `<span class="${isDebugOverride ? 'text-status-warn' : 'text-status-ok'} font-medium">Aktif di Taskbar (${window.audioDriverState.name || 'LG HT306'}${isDebugOverride ? ' — Mode Debug' : ''})</span>`;
          }
          if (btnInstallDriver) {
            const hasCableName = (window.audioDriverState.name && window.audioDriverState.name.includes("CABLE"));
            if (hasCableName) {
              btnInstallDriver.innerHTML = `<span class="material-symbols-outlined text-[16px]">draw</span><span>Sinkronkan Nama ke LG HT306</span>`;
              btnInstallDriver.className = "px-space-md py-1.5 bg-primary-container hover:bg-lg-red-hover text-text-primary rounded-lg font-caption text-caption font-semibold flex items-center gap-1.5 transition-colors cursor-pointer";
            } else {
              btnInstallDriver.innerHTML = `<span class="material-symbols-outlined text-[16px]">sync_alt</span><span>${window.audioDriverState.is_default ? 'Kembalikan Speaker PC' : 'Jadikan Default Taskbar'}</span>`;
              btnInstallDriver.className = "px-space-md py-1.5 bg-surface border border-border hover:bg-surface-variant text-text-primary rounded-lg font-caption text-caption font-semibold flex items-center gap-1.5 transition-colors cursor-pointer";
            }
          }
        }
      } else {
        if (audioDot) audioDot.className = "w-2 h-2 rounded-full bg-status-warn";
        if (audioText) {
          audioText.innerText = "+ Pasang Driver Taskbar";
          audioText.className = "font-code-sm text-code-sm text-status-warn font-medium";
        }
        if (settingDriverStatus) {
          settingDriverStatus.innerHTML = `<span class="text-status-warn font-medium">Belum Terpasang</span>`;
        }
        if (btnInstallDriver) {
          btnInstallDriver.innerHTML = `<span class="material-symbols-outlined text-[16px]">download</span><span>Pasang Driver Audio</span>`;
          btnInstallDriver.className = "px-space-md py-1.5 bg-primary-container hover:bg-lg-red-hover text-text-primary rounded-lg font-caption text-caption font-semibold flex items-center gap-1.5 transition-colors cursor-pointer";
        }
      }
    }
  }

  // --- EXPOSE TELEMETRY CONTRACTS ---
  window.updateTelemetry = handleNativeTelemetry;
  window.handleNativeTelemetry = handleNativeTelemetry;
  window.onNativeTelemetry = handleNativeTelemetry;

  // --- BROWSER FALLBACK POLLING ---
  function pollStatus() {
    if (window.isNativeWebView2) return;
    fetch("/api/status")
      .then((res) => {
        if (!res.ok) throw new Error("HTTP " + res.status);
        return res.json();
      })
      .then((data) => {
        handleNativeTelemetry(data);
      })
      .catch(() => {
        const headerDot = document.getElementById("header-status-dot");
        const headerText = document.getElementById("header-status-text");
        if (headerDot && headerText) {
          headerDot.className = "w-2 h-2 rounded-full bg-status-error";
          headerText.innerText = "Disconnected";
          headerText.className = "font-code-sm text-code-sm text-status-error font-medium";
        }
      });
  }

  function pollMeters() {
    if (window.isNativeWebView2) return;
    fetch("/api/meters")
      .then((res) => {
        if (!res.ok) throw new Error("HTTP " + res.status);
        return res.json();
      })
      .then((data) => {
        handleNativeTelemetry(data);
      })
      .catch(() => {});
  }

  if (!window.isNativeWebView2 && window.location.protocol.startsWith("http")) {
    setInterval(pollStatus, 500);
    pollStatus();
    setInterval(pollMeters, 60);
  }
})();
