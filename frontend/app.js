const state = {
  user: null,
  activeView: "add-file",
  files: [],
  selectedFileId: null,
  dataMode: "raw",
  dataPage: 1,
  dataPageSize: 15,
};

const views = [
  { id: "add-file", title: "添加文件", kicker: "Upload", roles: ["admin"] },
  { id: "saved-files", title: "已保存文件", kicker: "Files", roles: ["admin", "guest"] },
  { id: "overview", title: "数据概览", kicker: "Overview", roles: ["admin", "guest"] },
  { id: "data", title: "数据浏览", kicker: "Data", roles: ["admin"] },
  { id: "preprocess", title: "数据预处理", kicker: "Preprocess", roles: ["admin"] },
  { id: "stats", title: "统计分析", kicker: "Statistics", roles: ["admin", "guest"] },
  { id: "warnings", title: "预警报告", kicker: "Warnings", roles: ["admin"] },
  { id: "predict", title: "预测分析", kicker: "Prediction", roles: ["admin"] },
  { id: "logs", title: "日志", kicker: "Logs", roles: ["admin"] },
];

const fieldOptions = [
  ["temp", "水温"],
  ["salinity", "盐度"],
  ["ph", "pH"],
  ["do", "溶解氧"],
  ["precipitation", "降水量"],
  ["air_temp", "气温"],
];

const rawMarkLabels = {
  none: "不处理",
  delete: "将删除",
  fill: "缺失填充",
  repair: "异常修复",
  repair_fill: "修复并填充",
};

const operationMarkLabels = {
  none: "未操作",
  modified: "已修改",
  deleted: "已删除",
  added: "新增",
};

const rawFilterOptions = [
  ["all", "全部"],
  ["none", "不处理"],
  ["delete", "将删除"],
  ["fill", "缺失填充"],
  ["repair", "异常修复"],
  ["repair_fill", "修复并填充"],
];

const processedFilterOptions = [
  ["all", "全部"],
  ["none", "未操作"],
  ["modified", "已修改"],
  ["deleted", "已删除"],
  ["added", "新增"],
];

const $ = (selector) => document.querySelector(selector);

function h(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

function formatNumber(value) {
  if (value === null || value === undefined || value === "" || Number.isNaN(Number(value))) {
    return "-";
  }
  return Number(value).toLocaleString("zh-CN", { maximumFractionDigits: 4 });
}

function formatAny(value) {
  if (value === null || value === undefined || value === "") return "-";
  if (typeof value === "object") return JSON.stringify(value);
  return String(value);
}

function fieldLabel(key) {
  const found = fieldOptions.find(([value]) => value === key);
  return found ? found[1] : key;
}

function selectedFile() {
  return state.files.find((file) => file.id === state.selectedFileId) || null;
}

async function api(path, options = {}) {
  const response = await fetch(path, {
    ...options,
    headers: {
      "Content-Type": "application/json",
      ...(options.headers || {}),
    },
  });
  const payload = await response.json();
  if (!response.ok || payload.success === false) {
    throw new Error(payload.message || "请求失败");
  }
  return payload;
}

async function refreshFiles() {
  const data = await api("/api/files");
  state.files = data.files || [];
  if (state.selectedFileId && !state.files.some((file) => file.id === state.selectedFileId)) {
    state.selectedFileId = null;
  }
  if (!state.selectedFileId && state.files.length > 0) {
    state.selectedFileId = state.files[0].id;
  }
}

function setHeader(view) {
  $("#section-title").textContent = view.title;
  $("#section-kicker").textContent = view.kicker;
}

function renderNav() {
  const nav = $("#nav-list");
  nav.innerHTML = "";
  views
    .filter((view) => view.roles.includes(state.user.role))
    .forEach((view) => {
      const button = document.createElement("button");
      button.className = `nav-item${state.activeView === view.id ? " active" : ""}`;
      button.textContent = view.title;
      button.addEventListener("click", () => showView(view.id));
      nav.appendChild(button);
    });
}

function setContent(html) {
  $("#content").innerHTML = html;
}

function renderMetrics(items) {
  return `<div class="metrics">${items.map((item) => `
    <div class="metric">
      <span>${h(item.label)}</span>
      <strong>${h(item.value)}</strong>
    </div>
  `).join("")}</div>`;
}

function renderEmptyState() {
  setContent(`
    <section class="section-panel">
      <h3>工作区为空</h3>
      <p class="status-line">请先由管理员在“添加文件”中上传 CSV 文件。上传后系统会自动预处理，并保存未处理版本和已处理工作副本。</p>
    </section>
  `);
}

function requireFile() {
  if (!selectedFile()) {
    renderEmptyState();
    return false;
  }
  return true;
}

async function showView(id) {
  const view = views.find((item) => item.id === id);
  if (!view || !view.roles.includes(state.user.role)) return;

  state.activeView = id;
  setHeader(view);
  renderNav();
  setContent(`<p class="status-line">正在加载...</p>`);
  try {
    if (id !== "add-file") {
      await refreshFiles();
    }
    if (id === "add-file") await renderAddFile();
    if (id === "saved-files") await renderSavedFiles();
    if (id === "overview") await renderOverview();
    if (id === "data") await renderDataView();
    if (id === "preprocess") await renderPreprocess();
    if (id === "stats") await renderStats();
    if (id === "warnings") await renderWarnings();
    if (id === "predict") await renderPredict();
    if (id === "logs") await renderLogs();
  } catch (error) {
    setContent(`<p class="message">${h(error.message)}</p>`);
  }
}

async function renderAddFile() {
  await refreshFiles();
  setContent(`
    <section class="section-panel">
      <h3>添加 CSV 文件</h3>
      <form id="upload-form" class="form-grid">
        <label>选择文件
          <input id="upload-file" type="file" accept=".csv,text/csv" required>
        </label>
        <button type="submit">上传并自动预处理</button>
      </form>
      <p id="upload-status" class="status-line">上传后会生成 v000_raw、v001_preprocessed、原始预处理标记和操作日志。</p>
    </section>
    <section id="upload-result"></section>
  `);
  $("#upload-form").addEventListener("submit", handleUpload);
}

function readFileAsDataUrl(file) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(reader.result);
    reader.onerror = () => reject(reader.error);
    reader.readAsDataURL(file);
  });
}

async function handleUpload(event) {
  event.preventDefault();
  const file = $("#upload-file").files[0];
  if (!file) return;
  $("#upload-status").textContent = "正在上传并预处理...";
  const content = await readFileAsDataUrl(file);
  const payload = await api("/api/files", {
    method: "POST",
    body: JSON.stringify({
      filename: file.name,
      content_base64: content,
      actor: state.user.username,
    }),
  });
  await refreshFiles();
  state.selectedFileId = payload.file.id;
  state.dataMode = "processed";
  const summary = payload.preprocess || payload.file.preprocess_summary || {};
  $("#upload-status").textContent = "上传和预处理完成";
  $("#upload-result").innerHTML = renderMetrics([
    { label: "文件名", value: payload.file.original_name },
    { label: "总记录数", value: formatNumber(summary.total_records) },
    { label: "异常记录数", value: formatNumber(summary.abnormal_records) },
    { label: "缺失值数量", value: formatNumber(summary.missing_values) },
    { label: "填充值数量", value: formatNumber(summary.filled_values) },
    { label: "删除记录数", value: formatNumber(summary.deleted_records) },
  ]);
}

async function renderSavedFiles() {
  await refreshFiles();
  if (state.files.length === 0) {
    renderEmptyState();
    return;
  }
  setContent(`
    <section class="section-panel">
      <h3>文件列表</h3>
      <div class="file-list">
        ${state.files.map((file) => `
          <article class="file-card ${file.id === state.selectedFileId ? "selected" : ""}">
            <div>
              <strong>${h(file.original_name)}</strong>
              <p class="status-line">上传时间：${h(file.uploaded_at)} · 当前工作副本：${h(file.current_version)}</p>
              <p class="status-line">修改 ${formatNumber(file.modified_count)} 条 · 软删除 ${formatNumber(file.deleted_count)} 条 · 新增 ${formatNumber(file.added_count)} 条</p>
            </div>
            <div class="version-row">
              <button class="secondary-button select-file" data-id="${h(file.id)}">设为当前文件</button>
              <button class="version-chip open-data" data-file="${h(file.id)}" data-mode="raw">v000_raw 未处理版本</button>
              <button class="version-chip open-data" data-file="${h(file.id)}" data-mode="processed">v001_preprocessed 已处理版本</button>
            </div>
          </article>
        `).join("")}
      </div>
    </section>
  `);
  document.querySelectorAll(".select-file").forEach((button) => {
    button.addEventListener("click", () => {
      state.selectedFileId = button.dataset.id;
      showView("saved-files");
    });
  });
  document.querySelectorAll(".open-data").forEach((button) => {
    button.addEventListener("click", () => {
      state.selectedFileId = button.dataset.file;
      state.dataMode = button.dataset.mode;
      state.dataPage = 1;
      showView("data");
    });
  });
}

async function renderOverview() {
  if (!requireFile()) return;
  const file = selectedFile();
  const params = new URLSearchParams({ file_id: file.id, version: "current" });
  const data = await api(`/api/overview?${params.toString()}`);
  setContent(`
    <section class="section-panel">
      <h3>${h(file.original_name)}</h3>
      <p class="status-line">概览基于当前已处理工作副本；增删改痕迹见“数据浏览”和“日志”。</p>
    </section>
    ${renderMetrics([
      { label: "当前版本", value: data.version },
      { label: "总记录数", value: formatNumber(data.total_records) },
      { label: "有效记录数", value: formatNumber(data.valid_records) },
      { label: "异常记录数", value: formatNumber(data.abnormal_records) },
      { label: "缺失值数量", value: formatNumber(data.missing_values) },
      { label: "格式错误行", value: formatNumber(data.format_errors) },
      { label: "已修改记录", value: formatNumber(file.modified_count) },
      { label: "软删除记录", value: formatNumber(file.deleted_count) },
      { label: "新增记录", value: formatNumber(file.added_count) },
    ])}
  `);
}

function dataModeTabs() {
  return `
    <div class="mode-tabs">
      <button class="mode-tab ${state.dataMode === "raw" ? "active" : ""}" data-mode="raw">未处理版本文件</button>
      <button class="mode-tab ${state.dataMode === "processed" ? "active" : ""}" data-mode="processed">已处理版本文件</button>
    </div>
  `;
}

function operationFilterOptions() {
  const options = state.dataMode === "raw" ? rawFilterOptions : processedFilterOptions;
  return options.map(([value, label]) => `<option value="${value}">${label}</option>`).join("");
}

function markLegend() {
  if (state.dataMode === "raw") {
    return `
      <section class="mark-legend">
        <span class="mark-dot mark-none"></span>不处理
        <span class="mark-dot mark-delete"></span>将删除
        <span class="mark-dot mark-fill"></span>缺失填充
        <span class="mark-dot mark-repair"></span>异常修复
        <span class="mark-dot mark-repair_fill"></span>修复并填充
      </section>
    `;
  }
  return `
    <section class="mark-legend">
      <span class="mark-dot mark-op-none"></span>未操作
      <span class="mark-dot mark-op-modified"></span>已修改记录
      <span class="mark-dot mark-op-deleted"></span>已删除记录
      <span class="mark-dot mark-op-added"></span>新增记录
      <span class="mark-dot mark-cell-modified"></span>被修改字段
    </section>
  `;
}

async function renderDataView() {
  if (!requireFile()) return;
  const file = selectedFile();
  setContent(`
    <section class="section-panel">
      <h3>${h(file.original_name)}</h3>
      ${dataModeTabs()}
      <div class="toolbar">
        <label>筛选字段
          <select id="filter-field">
            <option value="">全部字段</option>
            ${fieldOptions.map(([value, label]) => `<option value="${value}">${label}</option>`).join("")}
          </select>
        </label>
        <label>最小值 <input id="filter-min" type="number" step="0.0001" placeholder="不限"></label>
        <label>最大值 <input id="filter-max" type="number" step="0.0001" placeholder="不限"></label>
        <label>排序
          <select id="sort-field">
            <option value="">默认顺序</option>
            ${fieldOptions.flatMap(([value, label]) => [
              `<option value="${value}_asc">${label}升序</option>`,
              `<option value="${value}_desc">${label}降序</option>`,
            ]).join("")}
          </select>
        </label>
        <label>处理方式
          <select id="operation-filter">${operationFilterOptions()}</select>
        </label>
        <button id="apply-filter">查询</button>
      </div>
    </section>
    ${markLegend()}
    <section class="table-wrap">
      <table>
        <thead><tr><th>记录号</th><th>标记</th><th>水温</th><th>盐度</th><th>pH</th><th>溶解氧</th><th>降水量</th><th>气温</th></tr></thead>
        <tbody id="data-body"></tbody>
      </table>
    </section>
    <div class="pager">
      <button id="prev-page" class="secondary-button">上一页</button>
      <button id="next-page" class="secondary-button">下一页</button>
      <span id="page-info" class="status-line"></span>
      <input id="jump-page" type="number" min="1" value="1">
      <button id="jump-button" class="secondary-button">跳转</button>
    </div>
    ${state.dataMode === "processed" ? renderMaintenanceForms() : `
      <section class="section-panel">
        <h3>未处理版本说明</h3>
        <p class="status-line">这里展示原始文件和预处理预测标记。增删改操作只作用在已处理版本文件中。</p>
      </section>
    `}
  `);
  document.querySelectorAll(".mode-tab").forEach((button) => {
    button.addEventListener("click", () => {
      state.dataMode = button.dataset.mode;
      state.dataPage = 1;
      renderDataView();
    });
  });
  $("#apply-filter").addEventListener("click", () => {
    state.dataPage = 1;
    loadDataPage();
  });
  $("#prev-page").addEventListener("click", () => {
    state.dataPage = Math.max(1, state.dataPage - 1);
    loadDataPage();
  });
  $("#next-page").addEventListener("click", () => {
    state.dataPage += 1;
    loadDataPage();
  });
  $("#jump-button").addEventListener("click", () => {
    state.dataPage = Number($("#jump-page").value || 1);
    loadDataPage();
  });
  if (state.dataMode === "processed") {
    $("#modify-form").addEventListener("submit", handleModify);
    $("#delete-form").addEventListener("submit", handleDelete);
    $("#add-form").addEventListener("submit", handleAdd);
  }
  await loadDataPage();
}

function renderMaintenanceForms() {
  return `
    <section class="section-panel">
      <h3>数据维护</h3>
      <div class="split">
        <form id="modify-form" class="form-grid">
          <label>记录号 <input name="row" type="number" min="1" required></label>
          <label>字段
            <select name="field">${fieldOptions.map(([value, label]) => `<option value="${value}">${label}</option>`).join("")}</select>
          </label>
          <label>新值 <input name="value" type="number" step="0.0001" required></label>
          <button type="submit">修改字段</button>
        </form>
        <form id="delete-form" class="form-grid">
          <label>记录号 <input name="row" type="number" min="1" required></label>
          <button type="submit" class="danger-button">软删除记录</button>
        </form>
      </div>
      <form id="add-form" class="form-grid wide-form">
        ${fieldOptions.map(([value, label]) => `
          <label>${label} <input name="${value}" type="number" step="0.0001" required></label>
        `).join("")}
        <button type="submit">新增记录</button>
      </form>
      <p id="data-message" class="status-line"></p>
    </section>
  `;
}

function buildDataQuery() {
  const params = new URLSearchParams();
  params.set("page", state.dataPage);
  params.set("page_size", state.dataPageSize);
  const field = $("#filter-field")?.value;
  const min = $("#filter-min")?.value;
  const max = $("#filter-max")?.value;
  const sort = $("#sort-field")?.value;
  const operationFilter = $("#operation-filter")?.value;
  if (field) params.set("field", field);
  if (min !== "") params.set("min", min);
  if (max !== "") params.set("max", max);
  if (sort) params.set("sort", sort);
  if (operationFilter) params.set("operation_filter", operationFilter);
  return params.toString();
}

async function loadDataPage() {
  const file = selectedFile();
  const endpoint = state.dataMode === "raw" ? "raw-data" : "processed-data";
  const data = await api(`/api/files/${file.id}/${endpoint}?${buildDataQuery()}`);
  state.dataPage = data.page;
  const labels = state.dataMode === "raw" ? rawMarkLabels : operationMarkLabels;
  $("#data-body").innerHTML = data.data.map((row) => {
    const mark = state.dataMode === "raw" ? (row.mark || "none") : (row.op_status || "none");
    const modifiedFields = Array.isArray(row.modified_fields) ? row.modified_fields : [];
    const rowClass = state.dataMode === "raw" ? `row-mark-${mark}` : `row-op-${mark}`;
    const cells = fieldOptions.map(([key]) => {
      const cellClass = state.dataMode === "processed" && mark === "modified" && modifiedFields.includes(key)
        ? "cell-modified"
        : "";
      return `<td class="${cellClass}">${formatNumber(row[key])}</td>`;
    }).join("");
    return `
      <tr class="${h(rowClass)}">
        <td>${row.row}</td>
        <td>${h(labels[mark] || mark)}</td>
        ${cells}
      </tr>
    `;
  }).join("");
  $("#page-info").textContent = `第 ${data.page} / ${data.total_pages} 页，共 ${data.total} 条`;
  $("#jump-page").value = data.page;
}

async function handleModify(event) {
  event.preventDefault();
  const form = new FormData(event.currentTarget);
  const file = selectedFile();
  const payload = {
    actor: state.user.username,
    row: Number(form.get("row")),
    field: form.get("field"),
    value: Number(form.get("value")),
  };
  await api(`/api/files/${file.id}/modify`, { method: "POST", body: JSON.stringify(payload) });
  await refreshFiles();
  $("#data-message").textContent = `已修改记录 ${payload.row} 的 ${fieldLabel(payload.field)}，操作已写入日志。`;
  await loadDataPage();
}

async function handleDelete(event) {
  event.preventDefault();
  const form = new FormData(event.currentTarget);
  const row = Number(form.get("row"));
  if (!confirm(`确认软删除记录 ${row}？该记录仍会在浏览中保留并显示为已删除。`)) return;
  const file = selectedFile();
  await api(`/api/files/${file.id}/delete`, {
    method: "POST",
    body: JSON.stringify({ actor: state.user.username, row }),
  });
  await refreshFiles();
  $("#data-message").textContent = `记录 ${row} 已标记为软删除，统计刷新时会自动排除。`;
  await loadDataPage();
}

async function handleAdd(event) {
  event.preventDefault();
  const form = new FormData(event.currentTarget);
  const record = {};
  fieldOptions.forEach(([key]) => {
    record[key] = Number(form.get(key));
  });
  const file = selectedFile();
  const result = await api(`/api/files/${file.id}/add`, {
    method: "POST",
    body: JSON.stringify({ actor: state.user.username, record }),
  });
  await refreshFiles();
  $("#data-message").textContent = `已新增记录 ${result.row}，操作已写入日志。`;
  event.currentTarget.reset();
  await loadDataPage();
}

async function renderPreprocess() {
  if (!requireFile()) return;
  const file = selectedFile();
  const logs = await api(`/api/files/${file.id}/logs`);
  const preprocessLogs = logs.logs.filter((item) => item.category === "preprocess");
  setContent(`
    <section class="section-panel">
      <h3>${h(file.original_name)}</h3>
      <button id="rerun-preprocess">重新预处理原始版本</button>
      <p id="preprocess-status" class="status-line">重新预处理会覆盖 v001_preprocessed，并重置已处理版本上的增删改标记。</p>
    </section>
    <section class="section-panel">
      <h3>预处理日志</h3>
      ${preprocessLogs.length ? renderLogList(preprocessLogs) : `<p class="status-line">暂无预处理日志。</p>`}
    </section>
  `);
  $("#rerun-preprocess").addEventListener("click", async () => {
    $("#preprocess-status").textContent = "正在重新预处理...";
    const result = await api(`/api/files/${file.id}/preprocess`, {
      method: "POST",
      body: JSON.stringify({ actor: state.user.username }),
    });
    await refreshFiles();
    $("#preprocess-status").textContent = `已重新生成 ${result.file.current_version}，操作标记已重置。`;
  });
}

async function renderStats() {
  if (!requireFile()) return;
  const file = selectedFile();
  setContent(`
    <section class="section-panel">
      <h3>${h(file.original_name)}</h3>
      <div class="toolbar">
        <button id="refresh-stats">刷新统计</button>
        <span id="stats-status" class="status-line">统计基于 v001_preprocessed，并默认排除软删除记录。</span>
      </div>
    </section>
    <section id="stats-result"></section>
  `);
  $("#refresh-stats").addEventListener("click", loadStats);
  await loadStats();
}

async function loadStats() {
  const file = selectedFile();
  $("#stats-status").textContent = "正在计算统计结果...";
  const params = new URLSearchParams({ file_id: file.id });
  const data = await api(`/api/stat-report?${params.toString()}`);
  const headers = fieldOptions.map(([, label]) => `<th>${label}</th>`).join("");
  const rows = data.correlation.map((row, index) => `
    <tr>
      <th>${fieldLabel(fieldOptions[index][0])}</th>
      ${row.map((value) => `<td>${formatNumber(value)}</td>`).join("")}
    </tr>
  `).join("");
  $("#stats-status").textContent = `已刷新，已排除软删除记录 ${formatNumber(data.excluded_deleted || 0)} 条。`;
  $("#stats-result").innerHTML = `
    <section class="section-panel">
      <h3>基本统计量</h3>
      <div class="table-wrap">
        <table>
          <thead><tr><th>参数</th><th>均值</th><th>最小值</th><th>最大值</th><th>标准差</th></tr></thead>
          <tbody>
            ${data.stats.map((item) => `
              <tr>
                <td>${fieldLabel(item.field)}</td>
                <td>${formatNumber(item.mean)}</td>
                <td>${formatNumber(item.min)}</td>
                <td>${formatNumber(item.max)}</td>
                <td>${formatNumber(item.stddev)}</td>
              </tr>
            `).join("")}
          </tbody>
        </table>
      </div>
    </section>
    <section class="section-panel">
      <h3>相关系数矩阵</h3>
      <div class="table-wrap">
        <table><thead><tr><th>参数</th>${headers}</tr></thead><tbody>${rows}</tbody></table>
      </div>
    </section>
  `;
}

async function renderWarnings() {
  if (!requireFile()) return;
  const file = selectedFile();
  const params = new URLSearchParams({ file_id: file.id });
  const data = await api(`/api/warnings?${params.toString()}`);
  setContent(`
    <section class="section-panel">
      <h3>${h(file.original_name)}</h3>
      <p class="status-line">预警基于 v001_preprocessed，并排除软删除记录 ${formatNumber(data.excluded_deleted || 0)} 条。</p>
    </section>
    ${renderMetrics([{ label: "预警数量", value: formatNumber(data.count) }])}
    <section class="table-wrap">
      <table>
        <thead><tr><th>时间</th><th>类型</th><th>数值</th><th>处理建议</th></tr></thead>
        <tbody>
          ${data.warnings.map((item) => `
            <tr><td>${h(item.time)}</td><td>${h(item.type)}</td><td>${formatNumber(item.value)}</td><td>${h(item.advice)}</td></tr>
          `).join("")}
        </tbody>
      </table>
    </section>
  `);
}

async function renderPredict() {
  if (!requireFile()) return;
  const file = selectedFile();
  const params = new URLSearchParams({ file_id: file.id });
  const data = await api(`/api/predict?${params.toString()}`);
  setContent(`
    <section class="section-panel">
      <h3>${h(file.original_name)}</h3>
      <p class="status-line">预测基于 v001_preprocessed，并排除软删除记录 ${formatNumber(data.excluded_deleted || 0)} 条。</p>
    </section>
    ${renderMetrics([
      { label: "主模型", value: "气温 → 溶解氧" },
      { label: "斜率 a", value: formatNumber(data.primary.slope) },
      { label: "截距 b", value: formatNumber(data.primary.intercept) },
      { label: "R²", value: formatNumber(data.primary.r2) },
      { label: "RMSE", value: formatNumber(data.primary.rmse) },
    ])}
    <section class="section-panel">
      <h3>多因子对比</h3>
      <div class="table-wrap">
        <table>
          <thead><tr><th>因子</th><th>斜率</th><th>截距</th><th>R²</th><th>RMSE</th></tr></thead>
          <tbody>
            ${data.models.map((item) => `
              <tr><td>${fieldLabel(item.x_field)}</td><td>${formatNumber(item.slope)}</td><td>${formatNumber(item.intercept)}</td><td>${formatNumber(item.r2)}</td><td>${formatNumber(item.rmse)}</td></tr>
            `).join("")}
          </tbody>
        </table>
      </div>
    </section>
  `);
}

function renderLogList(logs) {
  return `<div class="log-list">${logs.map((log) => `
    <article class="log-item">
      <strong>${h(log.time)} · ${h(log.type)} · ${h(log.actor || "-")}</strong>
      <p>${h(log.message || "")}</p>
      <small>
        记录号：${h(log.row || "-")} · 字段：${h(log.field || "-")} · 旧值：${h(formatAny(log.old_value))} · 新值：${h(formatAny(log.new_value))}
      </small>
      ${log.source_version || log.target_version ? `<small>${h(log.source_version || "")}${log.target_version ? ` → ${h(log.target_version)}` : ""}</small>` : ""}
    </article>
  `).join("")}</div>`;
}

async function renderLogs() {
  const data = await api("/api/logs");
  if (!data.files.length) {
    renderEmptyState();
    return;
  }
  setContent(`
    <section class="section-panel">
      <h3>按文件分组的操作日志</h3>
      ${data.files.map((file) => {
        const preprocessLogs = file.logs.filter((item) => item.category === "preprocess" || item.category === "version");
        const operationLogs = file.logs.filter((item) => item.category === "operation");
        return `
          <details class="file-log" ${file.id === state.selectedFileId ? "open" : ""}>
            <summary>${h(file.original_name)} · ${h(file.current_version)}</summary>
            <h4>预处理日志</h4>
            ${preprocessLogs.length ? renderLogList(preprocessLogs) : `<p class="status-line">暂无预处理日志。</p>`}
            <h4>操作日志</h4>
            ${operationLogs.length ? renderLogList(operationLogs) : `<p class="status-line">暂无增删改日志。</p>`}
          </details>
        `;
      }).join("")}
    </section>
  `);
}

$("#login-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  $("#login-message").textContent = "";
  const form = new FormData(event.currentTarget);
  try {
    const user = await api("/api/login", {
      method: "POST",
      body: JSON.stringify({
        username: form.get("username"),
        password: form.get("password"),
      }),
    });
    state.user = { ...user, username: user.username || form.get("username") };
    $("#login-screen").classList.add("hidden");
    $("#app-shell").classList.remove("hidden");
    $("#user-role").textContent = user.role === "admin" ? "管理员" : "访客";
    await refreshFiles();
    await showView(user.role === "admin" ? "add-file" : "saved-files");
  } catch (error) {
    $("#login-message").textContent = error.message;
  }
});

$("#logout-button").addEventListener("click", () => {
  state.user = null;
  $("#app-shell").classList.add("hidden");
  $("#login-screen").classList.remove("hidden");
});

$("#refresh-button").addEventListener("click", () => {
  if (state.user) showView(state.activeView);
});
