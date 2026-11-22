const MAX_NODES = 20;
const NODE_RADIUS = 17;

const SFG_PRESETS = [
  {
    name: "Two paths, four loops",
    nodes: 6,
    sink: 6,
    branches: [
      [1, 2, "1"],
      [2, 3, "2"],
      [3, 4, "3"],
      [4, 5, "4"],
      [5, 6, "1"],
      [3, 2, "-1"],
      [5, 4, "-0.5"],
      [4, 2, "-2"],
      [2, 5, "2"],
    ],
  },
  {
    name: "Non-touching loops",
    nodes: 7,
    sink: 7,
    branches: [
      [1, 2, "1"],
      [2, 3, "2"],
      [3, 4, "3"],
      [4, 5, "4"],
      [5, 6, "5"],
      [6, 7, "1"],
      [3, 2, "-1"],
      [5, 4, "-2"],
      [6, 6, "-3"],
      [6, 4, "-1"],
      [2, 6, "6"],
    ],
  },
];

const ROUTH_EXAMPLES = [
  { name: "Assignment example (unstable)", equation: "s^5 + s^4 + 10s^3 + 72s^2 + 152s + 240 = 0" },
  { name: "Stable, third order", equation: "s^3 + 6s^2 + 11s + 6 = 0" },
  { name: "Stable, fourth order", equation: "s^4 + 10s^3 + 35s^2 + 50s + 24 = 0" },
  { name: "Unstable, fourth order", equation: "s^4 + 2s^3 + 3s^2 + 4s + 5 = 0" },
];

const appState = {
  mode: "sfg",
  sfg: {
    nodes: 6,
    source: 0,
    sink: 5,
    rows: [],
    result: null,
  },
  routh: {
    input: ROUTH_EXAMPLES[0].equation,
    result: null,
  },
};

let activeGraphObserver = null;

function escapeHtml(text) {
  return String(text)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

function isValidGainText(text) {
  const trimmed = text.trim();
  if (!trimmed) {
    return false;
  }

  if (trimmed.includes("/")) {
    const parts = trimmed.split("/");
    if (parts.length !== 2) {
      return false;
    }
    const numeratorText = parts[0].trim();
    const denominatorText = parts[1].trim();
    const numerator = Number(numeratorText);
    const denominator = Number(denominatorText);
    return (
      numeratorText !== "" &&
      denominatorText !== "" &&
      Number.isFinite(numerator) &&
      Number.isFinite(denominator) &&
      denominator !== 0
    );
  }

  return Number.isFinite(Number(trimmed));
}

function clamp(value, minValue, maxValue) {
  return Math.max(minValue, Math.min(maxValue, value));
}

function wrapNegative(numberText) {
  const text = String(numberText);
  return text.startsWith("-") ? `(${text})` : text;
}

function loopName(index) {
  return `L${index + 1}`;
}

function formatLoopGroup(loopIndices) {
  return loopIndices.map(loopName).join(" × ");
}

function formatLoopList(loopIndices) {
  return loopIndices.length === 0 ? "none" : loopIndices.map(loopName).join(", ");
}

function formatLevelLabel(level) {
  if (level === 0) return "Σ Li";
  if (level === 1) return "Σ (Li × Lj)";
  if (level === 2) return "Σ (Li × Lj × Lk)";
  return `Σ (${level + 1}-loop products)`;
}

function getLevelSign(level) {
  return level % 2 === 0 ? "-" : "+";
}

function renderNodeSelectors() {
  const countInput = document.getElementById("sfg-node-count");
  const sourceSelect = document.getElementById("sfg-source");
  const sinkSelect = document.getElementById("sfg-sink");

  countInput.value = String(appState.sfg.nodes);

  const optionHtml = Array.from(
    { length: appState.sfg.nodes },
    (_, nodeIndex) => `<option value="${nodeIndex}">${nodeIndex + 1}</option>`
  ).join("");

  sourceSelect.innerHTML = optionHtml;
  sinkSelect.innerHTML = optionHtml;
  sourceSelect.value = String(appState.sfg.source);
  sinkSelect.value = String(appState.sfg.sink);
}

function renderBranches() {
  const container = document.getElementById("sfg-branches");
  container.innerHTML = appState.sfg.rows
    .map((row, index) => {
      const invalidClass = isValidGainText(row.gain) ? "" : "invalid";
      return `
        <div class="branch-row" data-index="${index}">
          <span class="branch-tag">w${index + 1}</span>
          <input class="pill-input num-input branch-from" type="number" min="1" max="${appState.sfg.nodes}" value="${row.from + 1}" aria-label="Branch ${index + 1} from">
          <input class="pill-input num-input branch-to" type="number" min="1" max="${appState.sfg.nodes}" value="${row.to + 1}" aria-label="Branch ${index + 1} to">
          <input class="pill-input gain-input branch-gain ${invalidClass}" type="text" value="${escapeHtml(row.gain)}" aria-label="Branch ${index + 1} gain">
          <button type="button" class="remove-btn" title="Remove branch" aria-label="Remove branch ${index + 1}">×</button>
        </div>
      `;
    })
    .join("");
}

function renderMode() {
  document.querySelectorAll(".nav-item").forEach((button) => {
    button.classList.toggle("active", button.dataset.mode === appState.mode);
  });
  document.getElementById("sfg-inputs").classList.toggle("hidden", appState.mode !== "sfg");
  document.getElementById("routh-inputs").classList.toggle("hidden", appState.mode !== "routh");
  renderResults();
}

async function analyzeSfg() {
  try {
    const response = await fetch("/api/sfg", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        nodes: appState.sfg.nodes,
        source: appState.sfg.source,
        sink: appState.sfg.sink,
        branches: appState.sfg.rows,
      }),
    });
    appState.sfg.result = await response.json();
  } catch (error) {
    appState.sfg.result = { ok: false, error: `Request failed: ${error.message}` };
  }

  if (appState.mode === "sfg") {
    renderResults();
  }
}

async function analyzeRouth() {
  try {
    const response = await fetch("/api/routh", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ equation: appState.routh.input }),
    });
    appState.routh.result = await response.json();
  } catch (error) {
    appState.routh.result = { ok: false, error: `Request failed: ${error.message}` };
  }

  if (appState.mode === "routh") {
    renderResults();
  }
}

function loadSfgPreset(preset) {
  appState.sfg.nodes = preset.nodes;
  appState.sfg.source = 0;
  appState.sfg.sink = preset.sink - 1;
  appState.sfg.rows = preset.branches.map(([fromNode, toNode, gain]) => ({
    from: fromNode - 1,
    to: toNode - 1,
    gain,
  }));

  renderNodeSelectors();
  renderBranches();
  analyzeSfg();
}

function buildTableHtml(headers, columnFractions, rows) {
  const colgroup = columnFractions
    .map((fraction) => `<col style="width: ${(fraction * 100).toFixed(2)}%">`)
    .join("");
  const headHtml = headers.map((header) => `<th>${escapeHtml(header)}</th>`).join("");
  const bodyHtml = rows
    .map((cells) => {
      const cellsHtml = cells
        .map((cell) => `<td class="${cell.cls || ""}">${escapeHtml(cell.text)}</td>`)
        .join("");
      return `<tr>${cellsHtml}</tr>`;
    })
    .join("");

  return `
    <table class="data-table">
      <colgroup>${colgroup}</colgroup>
      <thead><tr>${headHtml}</tr></thead>
      <tbody>${bodyHtml}</tbody>
    </table>
  `;
}

function buildCodeBlockHtml(lines, highlightLast) {
  const linesHtml = lines
    .map((line, index) => {
      const classes = ["code-line"];
      if (line.startsWith("Formula")) {
        classes.push("formula");
      }
      if (highlightLast && index + 1 === lines.length) {
        classes.push("highlight");
      }
      return `<div class="${classes.join(" ")}">${escapeHtml(line)}</div>`;
    })
    .join("");

  return `<div class="code-block">${linesHtml}</div>`;
}

function buildDeterminantLines(determinant, name) {
  let formula = `Formula: ${name} = 1`;
  for (let level = 0; level < determinant.levels.length; level++) {
    formula += ` ${getLevelSign(level)} ${formatLevelLabel(level)}`;
  }

  const lines = [formula];
  if (determinant.levels.length === 0) {
    lines.push(`No loops are available, so ${name} = 1`);
    return lines;
  }

  for (let level = 0; level < determinant.levels.length; level++) {
    const levelData = determinant.levels[level];
    const symbols = levelData.groups.map((group) => formatLoopGroup(group.loops)).join(" + ");
    const numbers = levelData.groups.map((group) => group.product_paren).join(" + ");
    lines.push(`${formatLevelLabel(level)} = ${symbols} = ${numbers} = ${levelData.sum_num}`);
  }

  let substituted = `Substitution: ${name} = 1`;
  for (let level = 0; level < determinant.levels.length; level++) {
    substituted += ` ${getLevelSign(level)} (${determinant.levels[level].sum_num})`;
  }
  lines.push(substituted);
  lines.push(`${name} = ${determinant.value_num}`);
  return lines;
}

function buildMasonLines(analysis) {
  if (analysis.terms.length === 0) {
    if (analysis.delta_zero) {
      return ["There is no forward path and Δ = 0, so the transfer function is undefined."];
    }
    return ["There is no forward path, so the numerator is 0 and T = 0."];
  }

  const symbolicTerms = analysis.terms.map((_, index) => `M${index + 1}Δ${index + 1}`).join(" + ");
  const lines = [`Formula: T = (${symbolicTerms}) / Δ`];

  analysis.terms.forEach((term, index) => {
    const pathGain = wrapNegative(analysis.paths[index].gain_num);
    const cofactorValue = wrapNegative(term.cofactor.value_num);
    lines.push(`M${index + 1}Δ${index + 1} = ${pathGain} × ${cofactorValue} = ${term.contribution_num}`);
  });

  const products = analysis.terms.map((term) => wrapNegative(term.contribution_num)).join(" + ");
  lines.push(`Numerator = ${products} = ${analysis.numerator_num}`);
  lines.push(`Substitution: T = (${analysis.numerator_num}) / (${analysis.delta.value_num})`);

  if (!analysis.delta_zero) {
    if (analysis.transfer_num.includes("/")) {
      lines.push(`Simplified: T = ${analysis.transfer_num} = ${analysis.transfer_decimal}`);
    } else {
      lines.push(`Simplified: T = ${analysis.transfer_num}`);
    }
  } else {
    lines.push("Δ = 0, so the transfer function is undefined.");
  }

  return lines;
}

function buildRouteRows(graph, routes, prefix) {
  return routes.map((route, index) => {
    const nodeLabels = route.nodes.map((nodeIndex) => String(nodeIndex + 1));
    const branchSymbols = route.branches.map((branchIndex) => `w${branchIndex + 1}`);
    const branchGains = route.branches.map((branchIndex) =>
      wrapNegative(graph.branches[branchIndex].gain_num)
    );
    const product =
      branchSymbols.length === 1
        ? branchSymbols[0]
        : `${branchSymbols.join(" × ")} = ${branchGains.join(" × ")}`;

    return [
      { text: `${prefix}${index + 1}`, cls: "tint-blue" },
      { text: nodeLabels.join(" -> ") },
      { text: product },
      { text: route.gain_num, cls: "tint-blue" },
    ];
  });
}

function renderSfgResults(container, data) {
  const { graph, analysis } = data;
  let html = "";

  if (!analysis.delta_zero) {
    html += `<h2 class="hero-value">T = ${escapeHtml(analysis.transfer_num)}</h2>`;
    html += `<p class="section-subtitle">Decimal value ${escapeHtml(analysis.transfer_decimal)}</p>`;
  } else {
    html += `<h2 class="hero-error">T is undefined because Δ = 0</h2>`;
  }

  html += `<p class="section-subtitle">${analysis.paths.length} forward path(s) &nbsp; ${analysis.loops.length} loop(s) &nbsp; Δ = ${escapeHtml(analysis.delta.value_num)}</p>`;

  html += `<h3 class="section-heading">Signal flow graph</h3>`;
  html += `<div class="graph-card"><canvas id="sfg-canvas"></canvas></div>`;

  html += `<h3 class="section-heading">1. Forward paths</h3>`;
  if (analysis.paths.length === 0) {
    html += `<p class="section-subtitle">There is no forward path from the input to the output.</p>`;
  } else {
    html += buildTableHtml(
      ["Gain", "Route", "Product of branch gains", "Value"],
      [0.10, 0.25, 0.45, 0.20],
      buildRouteRows(graph, analysis.paths, "M")
    );
  }

  html += `<h3 class="section-heading">2. Individual loops</h3>`;
  if (analysis.loops.length === 0) {
    html += `<p class="section-subtitle">This graph has no loops.</p>`;
  } else {
    html += buildTableHtml(
      ["Gain", "Route", "Product of branch gains", "Value"],
      [0.10, 0.25, 0.45, 0.20],
      buildRouteRows(graph, analysis.loops, "L")
    );
  }

  html += `<h3 class="section-heading">3. Non-touching loops</h3>`;
  if (analysis.delta.levels.length < 2) {
    html += `<p class="section-subtitle">No two loops in this graph are non-touching.</p>`;
  }
  for (let level = 1; level < analysis.delta.levels.length; level++) {
    html += `<p class="section-subtitle">Combinations of ${level + 1} non-touching loops</p>`;
    const rows = analysis.delta.levels[level].groups.map((group) => [
      { text: formatLoopGroup(group.loops) },
      { text: group.loops.map((loopIndex) => wrapNegative(analysis.loops[loopIndex].gain_num)).join(" × ") },
      { text: group.product_num, cls: "tint-blue" },
    ]);
    html += buildTableHtml(["Combination", "Product of loop gains", "Value"], [0.25, 0.55, 0.20], rows);
  }

  html += `<h3 class="section-heading">4. Graph determinant Δ</h3>`;
  html += buildCodeBlockHtml(buildDeterminantLines(analysis.delta, "Δ"), true);

  html += `<h3 class="section-heading">5. Path cofactors Δi</h3>`;
  if (analysis.terms.length === 0) {
    html += `<p class="section-subtitle">There are no forward paths, so no cofactors are needed.</p>`;
  }
  analysis.terms.forEach((term, index) => {
    html += `<p class="section-subtitle">Δ${index + 1} is Δ without the loops that touch path M${index + 1}</p>`;

    const touchingSet = new Set(term.touching);
    const nonTouchingLoops = [];
    for (let loopIndex = 0; loopIndex < analysis.loops.length; loopIndex++) {
      if (!touchingSet.has(loopIndex)) {
        nonTouchingLoops.push(loopIndex);
      }
    }

    const lines = [
      `Loops touching M${index + 1} (excluded): ${formatLoopList(term.touching)}`,
      `Loops not touching M${index + 1} (kept): ${formatLoopList(nonTouchingLoops)}`,
      ...buildDeterminantLines(term.cofactor, `Δ${index + 1}`),
    ];
    html += buildCodeBlockHtml(lines, true);
  });

  html += `<h3 class="section-heading">6. Mason's gain formula</h3>`;
  html += buildCodeBlockHtml(buildMasonLines(analysis), true);

  container.innerHTML = html;

  const canvas = document.getElementById("sfg-canvas");
  if (canvas) {
    drawSignalFlowGraph(canvas, graph);
    activeGraphObserver = new ResizeObserver(() => drawSignalFlowGraph(canvas, graph));
    activeGraphObserver.observe(canvas.parentElement);
  }
}

function buildDecisionLines(report) {
  const firstColumn = report.rows.map((row) => `${row.power_label}: ${row.values[0]}`).join(",  ");
  const lines = [
    "Rule: the number of sign changes in the first column equals the number of right half plane poles.",
    `First column: ${firstColumn}`,
  ];

  for (let index = 0; index + 1 < report.rows.length; index++) {
    const upperValue = report.rows[index].raw_values[0];
    const lowerValue = report.rows[index + 1].raw_values[0];
    const verdict = (upperValue > 0) === (lowerValue > 0) ? "no sign change" : "SIGN CHANGE";
    lines.push(
      `${report.rows[index].power_label} to ${report.rows[index + 1].power_label}: ${report.rows[index].values[0]} to ${report.rows[index + 1].values[0]}, ${verdict}`
    );
  }

  lines.push(`Total sign changes = ${report.sign_changes}`);
  if (report.is_stable) {
    lines.push("No sign changes, so there are no right half plane poles and the system is STABLE.");
  } else {
    lines.push(`${report.sign_changes} sign change(s), so there are ${report.sign_changes} right half plane pole(s) and the system is UNSTABLE.`);
  }
  return lines;
}

function renderRouthResults(container, report) {
  let html = `<h2 class="poly-title">${escapeHtml(report.polynomial)}</h2>`;

  if (report.is_stable) {
    html += `
      <div class="pill-row">
        <span class="status-pill good">STABLE</span>
        <span class="status-pill good">0 right half plane poles</span>
      </div>
    `;
  } else {
    html += `
      <div class="pill-row">
        <span class="status-pill bad">UNSTABLE</span>
        <span class="status-pill bad">${report.sign_changes} right half plane pole(s)</span>
      </div>
    `;
  }

  html += `<h3 class="section-heading">1. Routh table</h3>`;
  html += `<p class="section-subtitle">The first column decides stability. The sign column shows the sign of its entries.</p>`;

  const headers = ["Row", ...Array.from({ length: report.columns }, (_, col) => `Column ${col + 1}`), "Sign"];
  const middleFraction = 0.72 / report.columns;
  const fractions = [0.14, ...Array(report.columns).fill(middleFraction), 0.14];
  const routhRows = report.rows.map((row) => [
    { text: row.power_label, cls: "tint-blue" },
    ...row.values.map((value, col) => ({ text: value, cls: col === 0 ? "tint-blue" : "" })),
    { text: row.sign, cls: row.sign === "+" ? "tint-good" : "tint-bad" },
  ]);
  html += buildTableHtml(headers, fractions, routhRows);

  html += `<h3 class="section-heading">2. Calculation steps</h3>`;
  html += `<p class="section-subtitle">Entry = (pivot below × entry above-right - pivot above × entry below-right) / pivot below</p>`;
  const allSteps = report.rows.flatMap((row) => row.steps);
  html += buildCodeBlockHtml(allSteps, false);

  html += `<h3 class="section-heading">3. Stability decision</h3>`;
  html += buildCodeBlockHtml(buildDecisionLines(report), true);

  html += `<h3 class="section-heading">4. Numerical poles</h3>`;
  const poleRows = report.poles.map((pole, index) => {
    let locationText = "On imaginary axis";
    let colorClass = "tint-warn";
    if (pole.region === "Right") {
      locationText = "Right half plane";
      colorClass = "tint-bad";
    } else if (pole.region === "Left") {
      locationText = "Left half plane";
      colorClass = "tint-good";
    }
    return [
      { text: `p${index + 1}`, cls: "tint-blue" },
      { text: pole.re },
      { text: pole.im },
      { text: locationText, cls: colorClass },
    ];
  });
  html += buildTableHtml(["Pole", "Real part", "Imaginary part", "Location"], [0.12, 0.26, 0.26, 0.36], poleRows);

  html += `<h3 class="section-heading">5. Verification</h3>`;
  const verificationLines = [
    `Routh sign changes = ${report.sign_changes}`,
    `Numerical poles with real part greater than ${report.axis_tolerance} = ${report.numeric_rhp}`,
    report.numeric_rhp === report.sign_changes
      ? "Both counts agree, so the Routh result is confirmed."
      : "The counts differ. A pole may lie very close to the imaginary axis.",
  ];
  html += buildCodeBlockHtml(verificationLines, true);

  container.innerHTML = html;
}

function renderResults() {
  if (activeGraphObserver) {
    activeGraphObserver.disconnect();
    activeGraphObserver = null;
  }

  const container = document.getElementById("results-content");
  const result = appState.mode === "sfg" ? appState.sfg.result : appState.routh.result;

  if (!result) {
    container.innerHTML = `<p class="section-subtitle">Press Analyze to see the results.</p>`;
    return;
  }
  if (!result.ok) {
    container.innerHTML = `<p class="error-message">${escapeHtml(result.error)}</p>`;
    return;
  }

  if (appState.mode === "sfg") {
    renderSfgResults(container, result);
  } else {
    renderRouthResults(container, result);
  }
}

function getOrbPalette(isSource, isSink) {
  if (isSource) {
    return {
      coreLight: "rgba(244, 255, 250, 0.97)",
      midTint: "rgba(158, 240, 202, 0.87)",
      deepShade: "rgba(18, 138, 84, 0.91)",
      causticGlow: "rgba(185, 255, 225, 0.82)",
      rimStroke: "rgba(15, 118, 68, 0.84)",
      dispersionCyan: [56, 248, 212, 0.80],
      dispersionViolet: [110, 115, 245, 0.65],
      ambientShadow: "rgba(16, 110, 65, 0.16)",
      tagColor: "#137333",
    };
  }
  if (isSink) {
    return {
      coreLight: "rgba(255, 252, 242, 0.97)",
      midTint: "rgba(253, 218, 142, 0.87)",
      deepShade: "rgba(196, 108, 14, 0.91)",
      causticGlow: "rgba(255, 242, 165, 0.82)",
      rimStroke: "rgba(176, 92, 10, 0.84)",
      dispersionCyan: [255, 220, 95, 0.82],
      dispersionViolet: [236, 78, 155, 0.67],
      ambientShadow: "rgba(165, 85, 10, 0.16)",
      tagColor: "#b06000",
    };
  }
  return {
    coreLight: "rgba(244, 250, 255, 0.98)",
    midTint: "rgba(178, 228, 254, 0.87)",
    deepShade: "rgba(36, 128, 226, 0.91)",
    causticGlow: "rgba(175, 248, 255, 0.84)",
    rimStroke: "rgba(14, 104, 198, 0.84)",
    dispersionCyan: [52, 238, 255, 0.84],
    dispersionViolet: [168, 85, 247, 0.70],
    ambientShadow: "rgba(15, 75, 155, 0.16)",
    tagColor: "#0b57d0",
  };
}

function drawTaperedArc(ctx, centerX, centerY, radius, startAngle, endAngle, maxWidth, rgba) {
  const steps = 28;
  const [red, green, blue, baseAlpha] = rgba;
  ctx.lineCap = "round";

  for (let step = 0; step < steps; step++) {
    const startFrac = step / steps;
    const endFrac = (step + 1) / steps;
    const midFrac = 0.5 * (startFrac + endFrac);
    const profile = Math.sin(midFrac * Math.PI);
    const alpha = baseAlpha * profile * profile;
    if (alpha <= 0.005) {
      continue;
    }

    const angle0 = startAngle + (endAngle - startAngle) * startFrac;
    const angle1 = startAngle + (endAngle - startAngle) * endFrac;
    ctx.beginPath();
    ctx.moveTo(centerX + Math.cos(angle0) * radius, centerY + Math.sin(angle0) * radius);
    ctx.lineTo(centerX + Math.cos(angle1) * radius, centerY + Math.sin(angle1) * radius);
    ctx.strokeStyle = `rgba(${red}, ${green}, ${blue}, ${alpha.toFixed(3)})`;
    ctx.lineWidth = Math.max(maxWidth * (0.35 + 0.65 * profile), 0.6);
    ctx.stroke();
  }
}

function drawGlassOrb(ctx, centerX, centerY, radius, palette) {
  for (let layer = 0; layer < 5; layer++) {
    const frac = layer / 4;
    ctx.beginPath();
    ctx.arc(centerX + 1.4, centerY + 3.4, radius * (1.16 - 0.22 * frac), 0, Math.PI * 2);
    ctx.fillStyle = palette.ambientShadow;
    ctx.fill();
  }

  // offset radial focal point toward top-left for a lit sphere appearance
  const gradient = ctx.createRadialGradient(
    centerX - radius * 0.34,
    centerY - radius * 0.36,
    radius * 0.08,
    centerX + radius * 0.1,
    centerY + radius * 0.12,
    radius * 1.12
  );
  gradient.addColorStop(0.0, palette.coreLight);
  gradient.addColorStop(0.45, palette.midTint);
  gradient.addColorStop(0.92, palette.deepShade);
  gradient.addColorStop(1.0, palette.causticGlow);

  ctx.beginPath();
  ctx.arc(centerX, centerY, radius, 0, Math.PI * 2);
  ctx.fillStyle = gradient;
  ctx.fill();

  drawTaperedArc(ctx, centerX - 0.4, centerY - 0.4, radius - 1.1, -2.85, -0.25, 1.8, palette.dispersionCyan);
  drawTaperedArc(ctx, centerX + 0.4, centerY + 0.4, radius - 1.1, 0.25, 2.85, 1.8, palette.dispersionViolet);
  drawTaperedArc(ctx, centerX, centerY, radius * 0.78, 0.18, 1.42, 2.1, [255, 255, 255, 0.55]);

  ctx.beginPath();
  ctx.arc(centerX, centerY, radius - 0.4, 0, Math.PI * 2);
  ctx.strokeStyle = "rgba(255, 255, 255, 0.65)";
  ctx.lineWidth = 1.15;
  ctx.stroke();

  ctx.beginPath();
  ctx.arc(centerX, centerY, radius, 0, Math.PI * 2);
  ctx.strokeStyle = palette.rimStroke;
  ctx.lineWidth = 1.35;
  ctx.stroke();

  drawTaperedArc(ctx, centerX, centerY, radius * 0.72, -2.58, -1.18, 2.8, [255, 255, 255, 0.92]);

  const highlightX = centerX - radius * 0.34;
  const highlightY = centerY - radius * 0.36;
  ctx.beginPath();
  ctx.arc(highlightX, highlightY, radius * 0.19, 0, Math.PI * 2);
  ctx.fillStyle = "rgba(255, 255, 255, 0.49)";
  ctx.fill();

  ctx.beginPath();
  ctx.arc(highlightX - 0.5, highlightY - 0.5, radius * 0.11, 0, Math.PI * 2);
  ctx.fillStyle = "rgba(255, 255, 255, 0.96)";
  ctx.fill();

  ctx.beginPath();
  ctx.arc(centerX - radius * 0.12, centerY - radius * 0.56, radius * 0.065, 0, Math.PI * 2);
  ctx.fillStyle = "rgba(255, 255, 255, 0.82)";
  ctx.fill();
}

function drawLayeredStroke(ctx, tracePath) {
  const strokeLayers = [
    { width: 4.55, color: "rgba(11, 87, 208, 0.09)" },
    { width: 2.75, color: "rgba(88, 110, 135, 0.45)" },
    { width: 1.75, color: "rgba(54, 68, 84, 0.91)" },
  ];

  for (const layer of strokeLayers) {
    ctx.beginPath();
    tracePath();
    ctx.strokeStyle = layer.color;
    ctx.lineWidth = layer.width;
    ctx.lineCap = "round";
    ctx.stroke();
  }
}

function drawArrowHead(ctx, tipX, tipY, dirX, dirY) {
  const length = Math.hypot(dirX, dirY) || 1;
  const unitX = dirX / length;
  const unitY = dirY / length;
  const normalX = -unitY;
  const normalY = unitX;

  const arrowLength = 11.5;
  const halfWidth = 4.8;
  const baseX = tipX - unitX * arrowLength;
  const baseY = tipY - unitY * arrowLength;
  const leftWingX = baseX + normalX * halfWidth;
  const leftWingY = baseY + normalY * halfWidth;
  const rightWingX = baseX - normalX * halfWidth;
  const rightWingY = baseY - normalY * halfWidth;
  const notchX = tipX - unitX * (arrowLength * 0.76);
  const notchY = tipY - unitY * (arrowLength * 0.76);

  const glowBaseX = tipX - unitX * (arrowLength + 1.2);
  const glowBaseY = tipY - unitY * (arrowLength + 1.2);
  ctx.beginPath();
  ctx.moveTo(tipX + unitX * 0.6, tipY + unitY * 0.6);
  ctx.lineTo(glowBaseX + normalX * (halfWidth + 1.4), glowBaseY + normalY * (halfWidth + 1.4));
  ctx.lineTo(glowBaseX - normalX * (halfWidth + 1.4), glowBaseY - normalY * (halfWidth + 1.4));
  ctx.closePath();
  ctx.fillStyle = "rgba(11, 87, 208, 0.13)";
  ctx.fill();

  ctx.beginPath();
  ctx.moveTo(tipX, tipY);
  ctx.lineTo(leftWingX, leftWingY);
  ctx.lineTo(notchX, notchY);
  ctx.closePath();
  ctx.fillStyle = "rgba(64, 88, 116, 0.93)";
  ctx.fill();

  ctx.beginPath();
  ctx.moveTo(tipX, tipY);
  ctx.lineTo(notchX, notchY);
  ctx.lineTo(rightWingX, rightWingY);
  ctx.closePath();
  ctx.fillStyle = "rgba(46, 62, 80, 0.93)";
  ctx.fill();
}

function drawGainBadge(ctx, centerX, centerY, text) {
  ctx.font = '500 13px "Segoe UI", system-ui, sans-serif';
  const badgeWidth = ctx.measureText(text).width + 15;
  const badgeHeight = 23;
  const left = centerX - badgeWidth / 2;
  const top = centerY - badgeHeight / 2;

  ctx.beginPath();
  ctx.roundRect(left + 0.8, top + 1.8, badgeWidth, badgeHeight, 8);
  ctx.fillStyle = "rgba(15, 45, 90, 0.09)";
  ctx.fill();

  ctx.beginPath();
  ctx.roundRect(left, top, badgeWidth, badgeHeight, 8);
  ctx.fillStyle = "rgba(250, 253, 255, 0.91)";
  ctx.fill();
  ctx.strokeStyle = "rgba(145, 192, 242, 0.72)";
  ctx.lineWidth = 1;
  ctx.stroke();

  ctx.fillStyle = "#0b57d0";
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  ctx.fillText(text, centerX, centerY + 0.5);
}

function drawSignalFlowGraph(canvas, graph) {
  const rect = canvas.parentElement.getBoundingClientRect();
  const width = Math.max(rect.width, 200);
  const height = Math.max(rect.height, 200);

  // scale backing buffer by devicePixelRatio for sharp rendering on HiDPI displays
  const pixelRatio = window.devicePixelRatio || 1;
  canvas.width = Math.round(width * pixelRatio);
  canvas.height = Math.round(height * pixelRatio);

  const ctx = canvas.getContext("2d");
  ctx.setTransform(pixelRatio, 0, 0, pixelRatio, 0, 0);
  ctx.clearRect(0, 0, width, height);

  if (graph.nodes < 1) {
    return;
  }

  const marginX = 60;
  const horizontalSpan = Math.max(width - marginX * 2, 1);
  const baselineY = height / 2;

  const nodePositions = Array.from({ length: graph.nodes }, (_, index) => ({
    x: marginX + (horizontalSpan * index) / Math.max(graph.nodes - 1, 1),
    y: baselineY,
  }));

  const selfLoopCounts = Array(graph.nodes).fill(0);
  // defer badge rendering until after all curves and orbs are drawn
  const deferredLabels = [];

  graph.branches.forEach((branch, index) => {
    if (branch.from < 0 || branch.from >= graph.nodes || branch.to < 0 || branch.to >= graph.nodes) {
      return;
    }

    const label = `w${index + 1}: ${branch.gain_num}`;

    if (branch.from === branch.to) {
      const slot = selfLoopCounts[branch.from]++;
      const loopRadius = 16 + 6.5 * slot;
      const loopCenterX = nodePositions[branch.from].x;
      const loopCenterY = nodePositions[branch.from].y - (NODE_RADIUS + loopRadius - 3.5);
      const startAngle = Math.PI * 0.5 + 0.72;
      const endAngle = startAngle + Math.PI * 2 - 1.44;

      drawLayeredStroke(ctx, () => {
        ctx.arc(loopCenterX, loopCenterY, loopRadius, startAngle, endAngle);
      });

      const tipX = loopCenterX + Math.cos(endAngle) * loopRadius;
      const tipY = loopCenterY + Math.sin(endAngle) * loopRadius;
      drawArrowHead(ctx, tipX, tipY, -Math.sin(endAngle), Math.cos(endAngle));
      deferredLabels.push({ x: loopCenterX, y: loopCenterY - (loopRadius + 12.5), label });
      return;
    }

    const fromPos = nodePositions[branch.from];
    const toPos = nodePositions[branch.to];
    const hasReverseBranch = graph.branches.some(
      (other) => other.from === branch.to && other.to === branch.from
    );
    const parallelCount = graph.branches
      .slice(0, index)
      .filter((other) => other.from === branch.from && other.to === branch.to).length;

    let baseCurveRatio = 0;
    if (hasReverseBranch) {
      baseCurveRatio = 0.30;
    } else if (Math.abs(branch.from - branch.to) > 1) {
      baseCurveRatio = 0.38;
    }

    const deltaX = toPos.x - fromPos.x;
    const deltaY = toPos.y - fromPos.y;
    const distance = Math.max(Math.hypot(deltaX, deltaY), 1);
    const bendOffset = Math.min((baseCurveRatio + 0.22 * parallelCount) * distance, 240);
    const dirX = deltaX / distance;
    const dirY = deltaY / distance;
    const normalX = deltaY / distance;
    const normalY = -deltaX / distance;

    const apexX = fromPos.x + deltaX * 0.5 + normalX * bendOffset;
    const apexY = fromPos.y + deltaY * 0.5 + normalY * bendOffset;

    const startVecX = apexX - fromPos.x;
    const startVecY = apexY - fromPos.y;
    const startVecLen = Math.max(Math.hypot(startVecX, startVecY), 1);
    const startX = fromPos.x + (startVecX / startVecLen) * NODE_RADIUS;
    const startY = fromPos.y + (startVecY / startVecLen) * NODE_RADIUS;

    const endVecX = toPos.x - apexX;
    const endVecY = toPos.y - apexY;
    const endVecLen = Math.max(Math.hypot(endVecX, endVecY), 1);
    const endX = toPos.x - (endVecX / endVecLen) * NODE_RADIUS;
    const endY = toPos.y - (endVecY / endVecLen) * NODE_RADIUS;

    const control1X = startX + (apexX - startX) * 0.68;
    const control1Y = startY + (apexY - startY) * 0.68;
    const control2X = endX + (apexX - endX) * 0.68;
    const control2Y = endY + (apexY - endY) * 0.68;
    const isStraight = Math.abs(bendOffset) < 1e-3;

    drawLayeredStroke(ctx, () => {
      ctx.moveTo(startX, startY);
      if (isStraight) {
        ctx.lineTo(endX, endY);
      } else {
        ctx.bezierCurveTo(control1X, control1Y, control2X, control2Y, endX, endY);
      }
    });

    const arrivalX = isStraight ? dirX : endX - control2X;
    const arrivalY = isStraight ? dirY : endY - control2Y;
    drawArrowHead(ctx, endX, endY, arrivalX, arrivalY);

    const midX = isStraight
      ? 0.25 * startX + 0.5 * apexX + 0.25 * endX
      : 0.125 * startX + 0.375 * control1X + 0.375 * control2X + 0.125 * endX;
    const midY = isStraight
      ? 0.25 * startY + 0.5 * apexY + 0.25 * endY
      : 0.125 * startY + 0.375 * control1Y + 0.375 * control2Y + 0.125 * endY;

    deferredLabels.push({ x: midX + normalX * 15, y: midY + normalY * 15, label });
  });

  nodePositions.forEach((point, index) => {
    const isSource = index === graph.source;
    const isSink = !isSource && index === graph.sink;
    const palette = getOrbPalette(isSource, isSink);

    drawGlassOrb(ctx, point.x, point.y, NODE_RADIUS, palette);

    ctx.font = '500 15px "Segoe UI", system-ui, sans-serif';
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.fillStyle = "rgba(255, 255, 255, 0.76)";
    ctx.fillText(String(index + 1), point.x, point.y + 0.7);
    ctx.fillStyle = "#1f1f1f";
    ctx.fillText(String(index + 1), point.x, point.y);

    if (isSource || isSink) {
      ctx.font = '700 11px "Segoe UI", system-ui, sans-serif';
      ctx.fillStyle = palette.tagColor;
      ctx.fillText(isSource ? "INPUT" : "OUTPUT", point.x, point.y + NODE_RADIUS + 13);
    }
  });

  deferredLabels.forEach(({ x, y, label }) => {
    drawGainBadge(ctx, x, y, label);
  });
}

function init() {
  document.querySelectorAll(".nav-item").forEach((button) => {
    button.addEventListener("click", () => {
      appState.mode = button.dataset.mode;
      renderMode();
    });
  });

  const sfgPresetsEl = document.getElementById("sfg-presets");
  SFG_PRESETS.forEach((preset) => {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "chip-btn";
    button.textContent = preset.name;
    button.addEventListener("click", () => loadSfgPreset(preset));
    sfgPresetsEl.appendChild(button);
  });

  document.getElementById("sfg-node-count").addEventListener("change", (event) => {
    const nextCount = clamp(parseInt(event.target.value, 10) || 2, 2, MAX_NODES);
    const sinkWasLast = appState.sfg.sink + 1 === appState.sfg.nodes;
    appState.sfg.nodes = nextCount;
    appState.sfg.source = Math.min(appState.sfg.source, nextCount - 1);
    appState.sfg.sink = sinkWasLast ? nextCount - 1 : Math.min(appState.sfg.sink, nextCount - 1);
    appState.sfg.rows.forEach((row) => {
      row.from = Math.min(row.from, nextCount - 1);
      row.to = Math.min(row.to, nextCount - 1);
    });
    renderNodeSelectors();
    renderBranches();
  });

  document.getElementById("sfg-source").addEventListener("change", (event) => {
    appState.sfg.source = parseInt(event.target.value, 10) || 0;
  });

  document.getElementById("sfg-sink").addEventListener("change", (event) => {
    appState.sfg.sink = parseInt(event.target.value, 10) || 0;
  });

  const branchList = document.getElementById("sfg-branches");
  branchList.addEventListener("input", (event) => {
    const rowEl = event.target.closest(".branch-row");
    if (!rowEl) {
      return;
    }

    const index = parseInt(rowEl.dataset.index, 10);
    const row = appState.sfg.rows[index];
    if (event.target.classList.contains("branch-from")) {
      row.from = clamp(parseInt(event.target.value, 10) || 1, 1, appState.sfg.nodes) - 1;
    } else if (event.target.classList.contains("branch-to")) {
      row.to = clamp(parseInt(event.target.value, 10) || 1, 1, appState.sfg.nodes) - 1;
    } else if (event.target.classList.contains("branch-gain")) {
      row.gain = event.target.value;
      event.target.classList.toggle("invalid", !isValidGainText(row.gain));
    }
  });

  branchList.addEventListener("change", (event) => {
    const rowEl = event.target.closest(".branch-row");
    if (!rowEl) {
      return;
    }

    const index = parseInt(rowEl.dataset.index, 10);
    if (event.target.classList.contains("branch-from")) {
      event.target.value = String(appState.sfg.rows[index].from + 1);
    } else if (event.target.classList.contains("branch-to")) {
      event.target.value = String(appState.sfg.rows[index].to + 1);
    }
  });

  branchList.addEventListener("click", (event) => {
    if (!event.target.classList.contains("remove-btn")) {
      return;
    }
    const rowEl = event.target.closest(".branch-row");
    const index = parseInt(rowEl.dataset.index, 10);
    appState.sfg.rows.splice(index, 1);
    renderBranches();
  });

  document.getElementById("sfg-add-branch").addEventListener("click", () => {
    appState.sfg.rows.push({ from: 0, to: appState.sfg.nodes - 1, gain: "1" });
    renderBranches();
  });

  document.getElementById("sfg-analyze").addEventListener("click", analyzeSfg);

  const routhInput = document.getElementById("routh-equation");
  routhInput.value = appState.routh.input;
  routhInput.addEventListener("input", (event) => {
    appState.routh.input = event.target.value;
  });
  routhInput.addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
      analyzeRouth();
    }
  });
  document.getElementById("routh-analyze").addEventListener("click", analyzeRouth);

  const routhPresetsEl = document.getElementById("routh-presets");
  ROUTH_EXAMPLES.forEach((example) => {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "chip-btn";
    button.textContent = example.name;
    button.addEventListener("click", () => {
      appState.routh.input = example.equation;
      routhInput.value = example.equation;
      analyzeRouth();
    });
    routhPresetsEl.appendChild(button);
  });

  loadSfgPreset(SFG_PRESETS[0]);
  analyzeRouth();
  renderMode();
}

document.addEventListener("DOMContentLoaded", init);
