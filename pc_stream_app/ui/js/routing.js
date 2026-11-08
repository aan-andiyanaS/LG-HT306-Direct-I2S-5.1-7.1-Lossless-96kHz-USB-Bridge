/**
 * HT306 Home Theater - 6x6 Audio Channel Routing Matrix Engine
 * Manages input-to-DAC routing crosspoints, upmix/stereo presets, and surround channel swapping.
 */
(function () {
  "use strict";

  // --- ENSURE ROUTING STATE ---
  window.routeState = window.routeState || { 0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5 };

  const matrixInputs = ["FL", "FR", "C", "LFE", "SL", "SR"];
  const matrixOutputs = [
    { label: "DAC 1 [FL]", desc: "Front Left Pin 22" },
    { label: "DAC 2 [FR]", desc: "Front Right Pin 23" },
    { label: "DAC 3 [C]", desc: "Center Pin 24" },
    { label: "DAC 4 [LFE]", desc: "Subwoofer Pin 25" },
    { label: "DAC 5 [SL]", desc: "Surround Left Pin 26" },
    { label: "DAC 6 [SR]", desc: "Surround Right Pin 27" }
  ];

  /**
   * Render the 6x6 interactive matrix grid inside #matrix-tbody.
   */
  function renderMatrix() {
    const tbody = document.getElementById("matrix-tbody");
    if (!tbody) return;
    tbody.innerHTML = "";

    matrixOutputs.forEach((out, outIdx) => {
      const tr = document.createElement("tr");
      tr.className = outIdx % 2 === 0
        ? "bg-surface/40 hover:bg-surface-2/40 transition-colors"
        : "bg-surface-2/20 hover:bg-surface-2/40 transition-colors";

      let html = `
        <td class="p-space-sm">
          <div class="flex items-center justify-between">
            <div class="flex items-center gap-2">
              <span class="font-code-lg text-code-lg text-text-primary font-semibold">${out.label}</span>
              <span class="font-caption text-caption text-icon-gray hidden sm:inline">(${out.desc})</span>
            </div>
          </div>
        </td>
      `;

      matrixInputs.forEach((inCh, inIdx) => {
        const isActive = window.routeState[outIdx] === inIdx;
        html += `
          <td class="p-space-sm text-center">
            <button data-out="${outIdx}" data-in="${inIdx}" type="button" class="matrix-node group relative inline-flex items-center justify-center w-8 h-8 rounded-full transition-all duration-150 ${
              isActive ? "bg-primary-container text-text-primary ring-2 ring-primary/40 shadow-sm" : "bg-surface-2 hover:bg-surface-variant text-icon-gray"
            }" title="Hubungkan input ${matrixInputs[inIdx]} ke ${out.label}">
              <span class="w-2.5 h-2.5 rounded-full ${isActive ? "bg-text-primary" : "bg-icon-gray group-hover:bg-text-secondary"}"></span>
            </button>
          </td>
        `;
      });

      tr.innerHTML = html;
      tbody.appendChild(tr);
    });

    // Re-bind click handlers on matrix nodes
    tbody.querySelectorAll(".matrix-node").forEach((btn) => {
      btn.addEventListener("click", () => {
        const o = parseInt(btn.getAttribute("data-out"), 10);
        const i = parseInt(btn.getAttribute("data-in"), 10);
        window.routeState[o] = i;
        renderMatrix();
        window.sendNativeCommand("set_routing", { routing_map: window.routeState });
      });
    });
  }
  window.renderMatrix = renderMatrix;

  /**
   * Swap physical Surround Left (SL) and Surround Right (SR) channels and gains.
   */
  function swapSurroundChannels() {
    if (Array.isArray(window.trimsDb) && window.trimsDb.length >= 6) {
      const tmp = window.trimsDb[4];
      window.trimsDb[4] = window.trimsDb[5];
      window.trimsDb[5] = tmp;
    }
    window.sendNativeCommand("swap_surround_channels", { trims_db: window.trimsDb });
    if (typeof window.sendConfigUpdate === "function") {
      window.sendConfigUpdate({ trims_db: window.trimsDb });
    }
    alert("Channel SL (Surround Left) dan SR (Surround Right) berhasil ditukar.");
  }
  window.swapSurroundChannels = swapSurroundChannels;

  function initRouting() {
    renderMatrix();

    // Matrix Preset Handlers (Standard discrete 5.1, Swapped SL/SR, All-Stereo upmix)
    document.querySelectorAll(".btn-preset").forEach((btn) => {
      btn.addEventListener("click", () => {
        const preset = btn.getAttribute("data-preset");
        if (preset === "standard") {
          window.routeState = { 0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5 };
        } else if (preset === "swapped") {
          window.routeState = { 0: 0, 1: 1, 2: 2, 3: 3, 4: 5, 5: 4 };
        } else if (preset === "all-stereo") {
          window.routeState = { 0: 0, 1: 1, 2: 0, 3: 1, 4: 0, 5: 1 };
        }
        renderMatrix();
        window.sendNativeCommand("set_routing_preset", { preset: preset, routing_map: window.routeState });
      });
    });

    // Quick Swap SL <-> SR
    document.addEventListener("click", (e) => {
      const swapBtn = e.target.closest("#btn-quick-swap, [onclick*='Auto-Swapping SL and SR']");
      if (swapBtn) {
        e.preventDefault();
        swapSurroundChannels();
      }
    });
  }
  window.initRouting = initRouting;

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", initRouting);
  } else {
    initRouting();
  }
})();
