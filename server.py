from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse
import base64
import json
import mimetypes
import re
import shutil
import subprocess
import uuid
from datetime import datetime


ROOT = Path(__file__).resolve().parent
FRONTEND_DIR = ROOT / "frontend"
EXE = ROOT / "water_quality.exe"
WORKSPACE = ROOT / "workspace"
FILES_DIR = WORKSPACE / "files"
FIELD_KEYS = ["temp", "salinity", "ph", "do", "precipitation", "air_temp"]
RAW_MARKS_FILE = "raw_preprocess_marks.csv"
OPERATION_MARKS_FILE = "processed_operation_marks.json"
BACKUP_REASONS = {
    "before_modify",
    "before_delete",
    "before_add",
    "before_preprocess",
    "manual",
    "restore_guard",
}


def now_iso():
    return datetime.now().replace(microsecond=0).isoformat(sep=" ")


def safe_name(name):
    stem = Path(name).stem or "data"
    slug = re.sub(r"[^A-Za-z0-9_-]+", "_", stem).strip("_") or "data"
    return slug[:40]


def c_path(path):
    return path.resolve().as_posix()


def rel_path(file_dir, path):
    return path.resolve().relative_to(file_dir.resolve()).as_posix()


def ensure_workspace():
    FILES_DIR.mkdir(parents=True, exist_ok=True)


def run_c_command(args, timeout=180):
    if not EXE.exists():
        return {
            "success": False,
            "message": "water_quality.exe not found. Build the C program first.",
        }, 500

    completed = subprocess.run(
        [str(EXE), *args],
        cwd=str(ROOT),
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
    )
    raw = completed.stdout.strip()
    if not raw:
        return {
            "success": False,
            "message": completed.stderr.strip() or "C command produced no output",
        }, 500
    try:
        payload = json.loads(raw)
    except json.JSONDecodeError:
        return {
            "success": False,
            "message": "C command returned invalid JSON",
            "raw": raw,
        }, 500
    status = 200 if completed.returncode == 0 and payload.get("success", False) else 400
    return payload, status


def file_dir(file_id):
    if not re.fullmatch(r"[A-Za-z0-9_-]+", file_id or ""):
        raise ValueError("invalid file id")
    path = FILES_DIR / file_id
    if not path.exists():
        raise FileNotFoundError(file_id)
    return path


def metadata_path(directory):
    return directory / "metadata.json"


def logs_path(directory):
    return directory / "logs.jsonl"


def backups_dir(directory):
    return directory / "backups"


def reports_dir(directory):
    return directory / "reports"


def analysis_dir(directory):
    return directory / "analysis"


def load_metadata(directory):
    return json.loads(metadata_path(directory).read_text(encoding="utf-8"))


def save_metadata(directory, metadata):
    metadata_path(directory).write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )


def append_log(directory, entry):
    entry = {
        "time": now_iso(),
        **entry,
    }
    with logs_path(directory).open("a", encoding="utf-8") as fp:
        fp.write(json.dumps(entry, ensure_ascii=False) + "\n")


def read_logs(directory):
    path = logs_path(directory)
    if not path.exists():
        return []
    logs = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.strip():
            logs.append(json.loads(line))
    return logs


def find_version(metadata, version):
    version_id = version or "current"
    if version_id == "current":
        version_id = metadata.get("current_version", "v001_preprocessed")
    if version_id == "raw":
        version_id = "v000_raw"
    if version_id == "preprocessed":
        version_id = "v001_preprocessed"
    for item in metadata.get("versions", []):
        if item["id"] == version_id:
            return item
    raise FileNotFoundError(version_id)


def version_path(directory, metadata, version):
    item = find_version(metadata, version)
    return directory / item["path"], item


def raw_marks_path(directory, metadata):
    value = metadata.get("raw_preprocess_marks") or metadata.get("preprocess_marks")
    if value:
        return directory / value
    return directory / "preview" / RAW_MARKS_FILE


def operation_marks_path(directory, metadata):
    value = metadata.get("operation_marks")
    if value:
        return directory / value
    return directory / "preview" / OPERATION_MARKS_FILE


def empty_operation_marks():
    return {"rows": {}}


def load_operation_marks(directory, metadata):
    path = operation_marks_path(directory, metadata)
    if not path.exists():
        return empty_operation_marks()
    payload = json.loads(path.read_text(encoding="utf-8"))
    rows = payload.get("rows")
    if not isinstance(rows, dict):
        payload["rows"] = {}
    return payload


def save_operation_marks(directory, metadata, marks):
    path = operation_marks_path(directory, metadata)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(marks, ensure_ascii=False, indent=2), encoding="utf-8")


def mark_row(marks, row, status, field=None, fields=None):
    row_key = str(int(row))
    item = marks.setdefault("rows", {}).setdefault(row_key, {"status": status, "fields": []})
    current = item.get("status")
    if status == "deleted":
        item["status"] = "deleted"
        item["fields"] = []
        return
    if status == "added":
        item["status"] = "added"
        item["fields"] = list(fields or FIELD_KEYS)
        return
    if current not in ("added", "deleted"):
        item["status"] = "modified"
    if field and field not in item.setdefault("fields", []):
        item["fields"].append(field)


def operation_counts(marks):
    counts = {"operation_count": 0, "deleted_count": 0, "added_count": 0, "modified_count": 0}
    for item in marks.get("rows", {}).values():
        status = item.get("status")
        if status in ("modified", "deleted", "added"):
            counts["operation_count"] += 1
        if status == "deleted":
            counts["deleted_count"] += 1
        elif status == "added":
            counts["added_count"] += 1
        elif status == "modified":
            counts["modified_count"] += 1
    return counts


def update_operation_summary(directory, metadata, marks):
    metadata.update(operation_counts(marks))
    metadata["last_modified_at"] = now_iso()
    save_metadata(directory, metadata)


def initialize_operation_marks(directory, metadata):
    marks = empty_operation_marks()
    save_operation_marks(directory, metadata, marks)
    metadata.update(operation_counts(marks))


def create_backup(directory, metadata, reason, actor="admin"):
    if reason not in BACKUP_REASONS:
        raise ValueError("invalid backup reason")
    ensure_processed_ready(metadata)
    source_csv = processed_file_path(directory, metadata)
    source_marks = operation_marks_path(directory, metadata)
    if not source_csv.exists():
        raise FileNotFoundError("v001_preprocessed.csv")

    root = backups_dir(directory)
    root.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    backup_id = f"backup_{timestamp}_{reason}"
    target = root / backup_id
    index = 2
    while target.exists():
        backup_id = f"backup_{timestamp}_{reason}_{index}"
        target = root / backup_id
        index += 1
    target.mkdir(parents=True)

    backup_csv = target / "v001_preprocessed.csv"
    backup_marks = target / OPERATION_MARKS_FILE
    shutil.copyfile(source_csv, backup_csv)
    if source_marks.exists():
        shutil.copyfile(source_marks, backup_marks)
    else:
        backup_marks.write_text(json.dumps(empty_operation_marks(), ensure_ascii=False, indent=2), encoding="utf-8")

    info = {
        "id": backup_id,
        "reason": reason,
        "actor": actor,
        "created_at": now_iso(),
        "csv": "v001_preprocessed.csv",
        "operation_marks": OPERATION_MARKS_FILE,
        "operation_summary": operation_counts(load_operation_marks(directory, metadata)),
    }
    (target / "backup_info.json").write_text(json.dumps(info, ensure_ascii=False, indent=2), encoding="utf-8")
    metadata["last_backup_at"] = info["created_at"]
    save_metadata(directory, metadata)
    append_log(directory, {
        "category": "backup",
        "type": "create",
        "actor": actor,
        "backup_id": backup_id,
        "reason": reason,
        "message": f"已创建 {reason} 备份",
    })
    return info


def list_backups(directory):
    root = backups_dir(directory)
    if not root.exists():
        return []
    items = []
    for path in root.iterdir():
        if not path.is_dir():
            continue
        info_path = path / "backup_info.json"
        if info_path.exists():
            info = json.loads(info_path.read_text(encoding="utf-8"))
        else:
            info = {"id": path.name, "reason": "unknown", "actor": "-", "created_at": ""}
        info["has_csv"] = (path / "v001_preprocessed.csv").exists()
        info["has_operation_marks"] = (path / OPERATION_MARKS_FILE).exists()
        items.append(info)
    items.sort(key=lambda item: item.get("created_at") or item.get("id", ""), reverse=True)
    return items


def restore_backup(directory, backup_id, actor="admin"):
    if not re.fullmatch(r"[A-Za-z0-9_-]+", backup_id or ""):
        raise ValueError("invalid backup id")
    metadata = load_metadata(directory)
    backup_path = backups_dir(directory) / backup_id
    if not backup_path.exists() or not backup_path.is_dir():
        raise FileNotFoundError(backup_id)

    backup_csv = backup_path / "v001_preprocessed.csv"
    backup_marks = backup_path / OPERATION_MARKS_FILE
    if not backup_csv.exists():
        raise FileNotFoundError("backup csv")

    overview, status = run_c_command(["overview", "--input", c_path(backup_csv)])
    if status != 200:
        return overview, status

    guard = create_backup(directory, metadata, "restore_guard", actor)
    metadata = load_metadata(directory)
    target_csv = processed_file_path(directory, metadata)
    target_marks = operation_marks_path(directory, metadata)
    target_csv.parent.mkdir(parents=True, exist_ok=True)
    target_marks.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(backup_csv, target_csv)
    if backup_marks.exists():
        shutil.copyfile(backup_marks, target_marks)
    else:
        save_operation_marks(directory, metadata, empty_operation_marks())

    marks = load_operation_marks(directory, metadata)
    update_operation_summary(directory, metadata, marks)
    append_log(directory, {
        "category": "backup",
        "type": "restore",
        "actor": actor,
        "backup_id": backup_id,
        "guard_backup_id": guard["id"],
        "message": f"已从备份 {backup_id} 恢复当前工作副本",
    })
    return {
        "success": True,
        "backup_id": backup_id,
        "guard_backup_id": guard["id"],
        "overview": overview,
        "file": summarize_file(directory),
        "backups": list_backups(directory),
    }, 200


def summarize_file(directory):
    metadata = load_metadata(directory)
    logs = read_logs(directory)
    last_log = logs[-1] if logs else None
    summary = {
        "id": metadata["id"],
        "original_name": metadata["original_name"],
        "uploaded_at": metadata["uploaded_at"],
        "current_version": metadata.get("current_version", "v001_preprocessed"),
        "versions": metadata.get("versions", []),
        "preprocess_summary": metadata.get("preprocess_summary"),
        "raw_preprocess_marks": metadata.get("raw_preprocess_marks") or metadata.get("preprocess_marks"),
        "operation_marks": metadata.get("operation_marks"),
        "last_modified_at": metadata.get("last_modified_at"),
        "operation_count": metadata.get("operation_count", 0),
        "deleted_count": metadata.get("deleted_count", 0),
        "added_count": metadata.get("added_count", 0),
        "modified_count": metadata.get("modified_count", 0),
        "last_log": last_log,
    }
    return summary


def list_files():
    ensure_workspace()
    items = []
    for path in FILES_DIR.iterdir():
        if path.is_dir() and metadata_path(path).exists():
            items.append(summarize_file(path))
    items.sort(key=lambda item: item["uploaded_at"], reverse=True)
    return items


def decode_upload_content(value):
    if "," in value and value.split(",", 1)[0].startswith("data:"):
        value = value.split(",", 1)[1]
    return base64.b64decode(value)


def fixed_versions(directory, v000_path, v001_path, preprocess=None):
    return [
        {
            "id": "v000_raw",
            "label": "未处理版本",
            "kind": "raw",
            "path": rel_path(directory, v000_path),
            "created_at": now_iso(),
            "source_version": None,
            "operation": "upload",
        },
        {
            "id": "v001_preprocessed",
            "label": "已处理工作副本",
            "kind": "preprocessed",
            "path": rel_path(directory, v001_path),
            "created_at": now_iso(),
            "source_version": "v000_raw",
            "operation": "preprocess",
            "summary": preprocess,
        },
    ]


def create_uploaded_file(filename, content_base64, actor="admin"):
    ensure_workspace()
    file_id = f"{datetime.now().strftime('%Y%m%d_%H%M%S')}_{safe_name(filename)}_{uuid.uuid4().hex[:6]}"
    directory = FILES_DIR / file_id
    versions_dir = directory / "versions"
    preview_dir = directory / "preview"
    versions_dir.mkdir(parents=True, exist_ok=True)
    preview_dir.mkdir(parents=True, exist_ok=True)

    raw_path = directory / "raw.csv"
    raw_bytes = decode_upload_content(content_base64)
    raw_path.write_bytes(raw_bytes)

    v000_path = versions_dir / "v000_raw.csv"
    v001_path = versions_dir / "v001_preprocessed.csv"
    marks_path = preview_dir / RAW_MARKS_FILE
    op_marks_path = preview_dir / OPERATION_MARKS_FILE
    shutil.copyfile(raw_path, v000_path)

    metadata = {
        "id": file_id,
        "original_name": filename,
        "uploaded_at": now_iso(),
        "current_version": "v001_preprocessed",
        "versions": fixed_versions(directory, v000_path, v001_path, None),
        "raw_preprocess_marks": rel_path(directory, marks_path),
        "operation_marks": rel_path(directory, op_marks_path),
        "preprocess_summary": None,
        "last_modified_at": None,
    }
    initialize_operation_marks(directory, metadata)
    save_metadata(directory, metadata)
    append_log(directory, {
        "category": "version",
        "type": "upload",
        "actor": actor,
        "file_name": filename,
        "target_version": "v000_raw",
        "message": "上传原始文件",
    })

    preprocess, status = run_c_command([
        "preprocess",
        "--input", c_path(raw_path),
        "--output", c_path(v001_path),
        "--marks", c_path(marks_path),
    ])
    if status != 200:
        metadata["current_version"] = "v000_raw"
        save_metadata(directory, metadata)
        append_log(directory, {
            "category": "preprocess",
            "type": "preprocess_failed",
            "actor": actor,
            "message": preprocess.get("message", "预处理失败"),
        })
        return {**summarize_file(directory), "preprocess": preprocess}, status

    preprocess["input"] = rel_path(directory, raw_path)
    preprocess["output"] = rel_path(directory, v001_path)
    preprocess["marks"] = rel_path(directory, marks_path)
    metadata["versions"] = fixed_versions(directory, v000_path, v001_path, preprocess)
    metadata["current_version"] = "v001_preprocessed"
    metadata["preprocess_summary"] = preprocess
    save_metadata(directory, metadata)
    append_log(directory, {
        "category": "preprocess",
        "type": "auto_preprocess",
        "actor": actor,
        "source_version": "v000_raw",
        "target_version": "v001_preprocessed",
        "summary": preprocess,
        "message": "上传后自动预处理完成",
    })
    return {**summarize_file(directory), "preprocess": preprocess}, 200


def rerun_preprocess(directory, actor="admin"):
    metadata = load_metadata(directory)
    raw_path = directory / "raw.csv"
    v000_path, _ = version_path(directory, metadata, "v000_raw")
    v001_path = directory / "versions" / "v001_preprocessed.csv"
    marks_path = raw_marks_path(directory, metadata)
    if v001_path.exists():
        create_backup(directory, metadata, "before_preprocess", actor)
        metadata = load_metadata(directory)
    preprocess, status = run_c_command([
        "preprocess",
        "--input", c_path(raw_path),
        "--output", c_path(v001_path),
        "--marks", c_path(marks_path),
    ])
    if status != 200:
        return preprocess, status

    preprocess["input"] = rel_path(directory, raw_path)
    preprocess["output"] = rel_path(directory, v001_path)
    preprocess["marks"] = rel_path(directory, marks_path)
    metadata["versions"] = fixed_versions(directory, v000_path, v001_path, preprocess)
    metadata["current_version"] = "v001_preprocessed"
    metadata["preprocess_summary"] = preprocess
    initialize_operation_marks(directory, metadata)
    save_metadata(directory, metadata)
    append_log(directory, {
        "category": "preprocess",
        "type": "manual_preprocess",
        "actor": actor,
        "source_version": "v000_raw",
        "target_version": "v001_preprocessed",
        "summary": preprocess,
        "message": "重新预处理原始文件，已重置已处理版本的操作标记",
    })
    return {**summarize_file(directory), "preprocess": preprocess}, 200


def processed_file_path(directory, metadata):
    path, _ = version_path(directory, metadata, "v001_preprocessed")
    return path


def ensure_processed_ready(metadata):
    if metadata.get("current_version") == "v000_raw":
        raise ValueError("preprocessed version is not available")


def apply_modify(directory, body):
    metadata = load_metadata(directory)
    ensure_processed_ready(metadata)
    actor = body.get("actor", "admin")
    row = int(body.get("row", 0))
    field = str(body.get("field", ""))
    value = body.get("value", "")
    marks = load_operation_marks(directory, metadata)
    if marks.get("rows", {}).get(str(row), {}).get("status") == "deleted":
        return {"success": False, "message": "deleted row cannot be modified"}, 400
    path = processed_file_path(directory, metadata)
    create_backup(directory, metadata, "before_modify", actor)
    result, status = run_c_command([
        "modify",
        "--input", c_path(path),
        "--row", str(row),
        "--field", field,
        "--value", str(value),
    ])
    if status != 200:
        return result, status

    mark_row(marks, row, "modified", field=field)
    save_operation_marks(directory, metadata, marks)
    update_operation_summary(directory, metadata, marks)
    append_log(directory, {
        "category": "operation",
        "type": "modify",
        "actor": actor,
        "row": row,
        "field": field,
        "old_value": result.get("old_value"),
        "new_value": result.get("new_value"),
        "message": f"修改记录 {row} 的 {field}",
    })
    return {**result, "file": summarize_file(directory), "operation_summary": operation_counts(marks)}, 200


def apply_delete(directory, body):
    metadata = load_metadata(directory)
    ensure_processed_ready(metadata)
    actor = body.get("actor", "admin")
    path = processed_file_path(directory, metadata)
    create_backup(directory, metadata, "before_delete", actor)
    args = ["delete", "--input", c_path(path)]
    if "row" in body:
        args.extend(["--row", str(body["row"])])
    if "field" in body:
        args.extend(["--field", str(body["field"])])
    if "min" in body:
        args.extend(["--min", str(body["min"])])
    if "max" in body:
        args.extend(["--max", str(body["max"])])

    result, status = run_c_command(args)
    if status != 200:
        return result, status

    rows = result.get("rows") or ([int(body["row"])] if "row" in body else [])
    marks = load_operation_marks(directory, metadata)
    for row in rows:
        mark_row(marks, int(row), "deleted")
    save_operation_marks(directory, metadata, marks)
    update_operation_summary(directory, metadata, marks)
    append_log(directory, {
        "category": "operation",
        "type": "delete",
        "actor": actor,
        "row": ",".join(str(row) for row in rows),
        "field": None,
        "old_value": None,
        "new_value": None,
        "affected": result.get("deleted", len(rows)),
        "message": f"软删除记录 {','.join(str(row) for row in rows)}",
    })
    return {**result, "file": summarize_file(directory), "operation_summary": operation_counts(marks)}, 200


def apply_add(directory, body):
    metadata = load_metadata(directory)
    ensure_processed_ready(metadata)
    actor = body.get("actor", "admin")
    record = body.get("record") if isinstance(body.get("record"), dict) else body
    create_backup(directory, metadata, "before_add", actor)
    args = ["add", "--input", c_path(processed_file_path(directory, metadata))]
    for key in FIELD_KEYS:
        if key not in record:
            return {"success": False, "message": f"{key} is required"}, 400
        args.extend([f"--{key}", str(record[key])])

    result, status = run_c_command(args)
    if status != 200:
        return result, status

    row = int(result.get("row"))
    marks = load_operation_marks(directory, metadata)
    mark_row(marks, row, "added", fields=FIELD_KEYS)
    save_operation_marks(directory, metadata, marks)
    update_operation_summary(directory, metadata, marks)
    append_log(directory, {
        "category": "operation",
        "type": "add",
        "actor": actor,
        "row": row,
        "field": None,
        "old_value": None,
        "new_value": {key: record[key] for key in FIELD_KEYS},
        "message": f"新增记录 {row}",
    })
    return {**result, "file": summarize_file(directory), "operation_summary": operation_counts(marks)}, 200


def apply_filter(directory, body):
    metadata = load_metadata(directory)
    ensure_processed_ready(metadata)
    actor = body.get("actor", "admin")
    window = int(body.get("window", 0))
    if window not in (3, 5, 7, 9, 11):
        return {"success": False, "message": "window must be 3, 5, 7, 9, or 11"}, 400
    output_dir = analysis_dir(directory)
    output_dir.mkdir(parents=True, exist_ok=True)
    output_path = output_dir / f"filter_window_{window}.csv"
    payload, status = run_c_command([
        "filter",
        "--input", c_path(processed_file_path(directory, metadata)),
        "--output", c_path(output_path),
        "--window", str(window),
    ])
    if status == 200:
        payload["output"] = rel_path(directory, output_path)
        append_log(directory, {
            "category": "analysis",
            "type": "filter",
            "actor": actor,
            "window": window,
            "output": payload["output"],
            "message": f"执行窗口 {window} 的移动平均滤波",
        })
    return payload, status


def analysis_payload(directory, metadata, command):
    if command == "overview":
        path, _ = version_path(directory, metadata, "v001_preprocessed")
        args = [command, "--input", c_path(path)]
    else:
        path, _ = version_path(directory, metadata, "v001_preprocessed")
        args = [command, "--input", c_path(path), "--op-marks", c_path(operation_marks_path(directory, metadata))]
    return run_c_command(args)


def field_name(key):
    labels = {
        "temp": "水温",
        "salinity": "盐度",
        "ph": "pH",
        "do": "溶解氧",
        "precipitation": "降水量",
        "air_temp": "气温",
    }
    return labels.get(key, key)


def render_report_text(command, file_summary, payload):
    lines = [
        f"文件：{file_summary['original_name']}",
        f"报告类型：{command}",
        f"生成时间：{now_iso()}",
        "",
    ]
    if command == "overview":
        lines.extend([
            f"总记录数：{payload.get('total_records')}",
            f"有效记录数：{payload.get('valid_records')}",
            f"异常记录数：{payload.get('abnormal_records')}",
            f"缺失值数量：{payload.get('missing_values')}",
            f"格式错误行：{payload.get('format_errors')}",
        ])
    elif command == "stats":
        lines.append("基本统计量：")
        for item in payload.get("stats", []):
            lines.append(
                f"- {field_name(item.get('field'))}: 均值 {item.get('mean')}, "
                f"最小值 {item.get('min')}, 最大值 {item.get('max')}, 标准差 {item.get('stddev')}"
            )
        lines.append(f"已排除软删除记录：{payload.get('excluded_deleted', 0)}")
    elif command == "warnings":
        lines.append(f"预警数量：{payload.get('count', 0)}")
        for item in payload.get("warnings", []):
            lines.append(
                f"- {item.get('time')} | {item.get('type')} | {item.get('value')} | {item.get('advice')}"
            )
        lines.append(f"已排除软删除记录：{payload.get('excluded_deleted', 0)}")
    elif command == "predict":
        primary = payload.get("primary", {})
        lines.extend([
            "主模型：气温 -> 溶解氧",
            f"斜率：{primary.get('slope')}",
            f"截距：{primary.get('intercept')}",
            f"R²：{primary.get('r2')}",
            f"RMSE：{primary.get('rmse')}",
            f"已排除软删除记录：{payload.get('excluded_deleted', 0)}",
            "",
            "多因子模型：",
        ])
        for item in payload.get("models", []):
            lines.append(
                f"- {field_name(item.get('x_field'))}: slope={item.get('slope')}, "
                f"intercept={item.get('intercept')}, r2={item.get('r2')}, rmse={item.get('rmse')}"
            )
    return "\n".join(lines) + "\n"


def list_reports(directory):
    root = reports_dir(directory)
    if not root.exists():
        return []
    items = []
    for path in sorted(root.glob("*_report.txt")):
        stat = path.stat()
        items.append({
            "name": path.name,
            "path": rel_path(directory, path),
            "size": stat.st_size,
            "modified_at": datetime.fromtimestamp(stat.st_mtime).replace(microsecond=0).isoformat(sep=" "),
        })
    return items


def generate_reports(directory, actor="admin"):
    metadata = load_metadata(directory)
    ensure_processed_ready(metadata)
    root = reports_dir(directory)
    root.mkdir(parents=True, exist_ok=True)
    file_summary = summarize_file(directory)
    commands = [
        ("overview", "overview_report.txt"),
        ("stats", "stat_report.txt"),
        ("warnings", "warning_report.txt"),
        ("predict", "predict_report.txt"),
    ]
    generated = []
    for command, filename in commands:
        payload, status = analysis_payload(directory, metadata, command)
        if status != 200:
            return payload, status
        path = root / filename
        path.write_text(render_report_text(command, file_summary, payload), encoding="utf-8")
        generated.append({
            "name": filename,
            "path": rel_path(directory, path),
        })
    append_log(directory, {
        "category": "report",
        "type": "export",
        "actor": actor,
        "files": generated,
        "message": "导出概览、统计、预警、预测报告文件",
    })
    return {"success": True, "reports": list_reports(directory), "file": summarize_file(directory)}, 200


class WaterQualityHandler(SimpleHTTPRequestHandler):
    server_version = "WaterQualityHTTP/3.0"

    def log_message(self, format, *args):
        return

    def send_json(self, payload, status=200):
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def read_json_body(self):
        length = int(self.headers.get("Content-Length", "0"))
        if length == 0:
            return {}
        return json.loads(self.rfile.read(length).decode("utf-8"))

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path.startswith("/api/"):
            self.safe_api(lambda: self.handle_api_get(parsed))
            return
        self.serve_static(parsed.path)

    def do_POST(self):
        parsed = urlparse(self.path)
        if not parsed.path.startswith("/api/"):
            self.send_json({"success": False, "message": "not found"}, 404)
            return
        self.safe_api(lambda: self.handle_api_post(parsed))

    def safe_api(self, fn):
        try:
            fn()
        except json.JSONDecodeError:
            self.send_json({"success": False, "message": "invalid JSON body"}, 400)
        except FileNotFoundError:
            self.send_json({"success": False, "message": "file or version not found"}, 404)
        except ValueError as error:
            self.send_json({"success": False, "message": str(error)}, 400)
        except Exception as error:
            self.send_json({"success": False, "message": str(error)}, 500)

    def serve_static(self, path):
        if path == "/":
            path = "/index.html"
        target = (FRONTEND_DIR / path.lstrip("/")).resolve()
        if not str(target).startswith(str(FRONTEND_DIR.resolve())) or not target.exists():
            self.send_error(404)
            return
        content_type = mimetypes.guess_type(str(target))[0] or "application/octet-stream"
        body = target.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def handle_api_get(self, parsed):
        query = parse_qs(parsed.query)
        segments = [part for part in parsed.path.split("/") if part]

        if parsed.path == "/api/files":
            self.send_json({"success": True, "files": list_files()})
            return
        if parsed.path == "/api/logs":
            files = []
            for item in list_files():
                directory = file_dir(item["id"])
                files.append({**item, "logs": read_logs(directory)})
            self.send_json({"success": True, "files": files})
            return
        if parsed.path == "/api/overview":
            self.send_analysis("overview", query)
            return
        if parsed.path == "/api/stat-report":
            self.send_analysis("stats", query)
            return
        if parsed.path == "/api/warnings":
            self.send_analysis("warnings", query)
            return
        if parsed.path == "/api/predict":
            self.send_analysis("predict", query)
            return

        if len(segments) >= 3 and segments[0] == "api" and segments[1] == "files":
            directory = file_dir(segments[2])
            metadata = load_metadata(directory)
            if len(segments) == 3:
                self.send_json({"success": True, "file": summarize_file(directory)})
                return
            if len(segments) == 4 and segments[3] == "logs":
                self.send_json({"success": True, "logs": read_logs(directory), "file": summarize_file(directory)})
                return
            if len(segments) == 4 and segments[3] == "operation-summary":
                self.send_json({"success": True, **operation_counts(load_operation_marks(directory, metadata))})
                return
            if len(segments) == 4 and segments[3] == "backups":
                self.send_json({"success": True, "backups": list_backups(directory), "file": summarize_file(directory)})
                return
            if len(segments) == 4 and segments[3] == "reports":
                self.send_json({"success": True, "reports": list_reports(directory), "file": summarize_file(directory)})
                return
            if len(segments) == 4 and segments[3] == "raw-data":
                self.send_file_data(directory, metadata, query, "raw")
                return
            if len(segments) == 4 and segments[3] == "processed-data":
                self.send_file_data(directory, metadata, query, "processed")
                return
            if len(segments) == 4 and segments[3] == "data":
                view = query.get("view", [None])[0]
                if not view:
                    requested = query.get("version", ["current"])[0]
                    view = "raw" if requested in ("raw", "v000_raw") else "processed"
                self.send_file_data(directory, metadata, query, view)
                return
        self.send_json({"success": False, "message": "unknown API endpoint"}, 404)

    def handle_api_post(self, parsed):
        body = self.read_json_body()
        segments = [part for part in parsed.path.split("/") if part]

        if parsed.path == "/api/login":
            payload, status = run_c_command([
                "login",
                "--username", str(body.get("username", "")),
                "--password", str(body.get("password", "")),
            ])
            self.send_json(payload, status)
            return
        if parsed.path == "/api/files":
            filename = body.get("filename")
            content = body.get("content_base64") or body.get("content")
            if not filename or not content:
                self.send_json({"success": False, "message": "filename and content are required"}, 400)
                return
            payload, status = create_uploaded_file(filename, content, body.get("actor", "admin"))
            self.send_json({"success": status == 200, "file": payload, "preprocess": payload.get("preprocess")}, status)
            return

        if len(segments) >= 4 and segments[0] == "api" and segments[1] == "files":
            directory = file_dir(segments[2])
            action = segments[3]
            if action == "preprocess":
                payload, status = rerun_preprocess(directory, body.get("actor", "admin"))
                self.send_json({"success": status == 200, "file": payload, "preprocess": payload.get("preprocess")}, status)
                return
            if action == "modify":
                payload, status = apply_modify(directory, body)
                self.send_json(payload, status)
                return
            if action == "delete":
                payload, status = apply_delete(directory, body)
                self.send_json(payload, status)
                return
            if action == "add":
                payload, status = apply_add(directory, body)
                self.send_json(payload, status)
                return
            if action == "filter":
                payload, status = apply_filter(directory, body)
                self.send_json(payload, status)
                return
            if action == "backup":
                metadata = load_metadata(directory)
                info = create_backup(directory, metadata, "manual", body.get("actor", "admin"))
                self.send_json({"success": True, "backup": info, "backups": list_backups(directory), "file": summarize_file(directory)})
                return
            if action == "restore":
                payload, status = restore_backup(directory, body.get("backup_id", ""), body.get("actor", "admin"))
                self.send_json(payload, status)
                return
            if action == "reports":
                payload, status = generate_reports(directory, body.get("actor", "admin"))
                self.send_json(payload, status)
                return
        self.send_json({"success": False, "message": "unknown API endpoint"}, 404)

    def send_analysis(self, command, query):
        file_id = query.get("file_id", [None])[0]
        if not file_id:
            self.send_json({"success": False, "message": "please add and select a file first"}, 400)
            return
        directory = file_dir(file_id)
        metadata = load_metadata(directory)
        if command == "overview":
            version = query.get("version", ["current"])[0]
            path, version_item = version_path(directory, metadata, version)
            args = [command, "--input", c_path(path)]
        else:
            path, version_item = version_path(directory, metadata, "v001_preprocessed")
            args = [command, "--input", c_path(path), "--op-marks", c_path(operation_marks_path(directory, metadata))]
        payload, status = run_c_command(args)
        if status == 200:
            payload["version"] = version_item["id"]
            payload["file_id"] = file_id
            payload["input"] = version_item["path"]
            payload["operation_summary"] = operation_counts(load_operation_marks(directory, metadata))
        self.send_json(payload, status)

    def send_file_data(self, directory, metadata, query, view):
        if view == "raw":
            path, version_item = version_path(directory, metadata, "v000_raw")
            args = [
                "query",
                "--input", c_path(path),
                "--view", "raw",
                "--raw-marks", c_path(raw_marks_path(directory, metadata)),
            ]
        else:
            path, version_item = version_path(directory, metadata, "v001_preprocessed")
            args = [
                "query",
                "--input", c_path(path),
                "--view", "processed",
                "--op-marks", c_path(operation_marks_path(directory, metadata)),
            ]

        self.add_query_arg(args, query, "page", "--page")
        self.add_query_arg(args, query, "page_size", "--page-size")
        self.add_query_arg(args, query, "pageSize", "--page-size")
        self.add_query_arg(args, query, "sort", "--sort")
        self.add_query_arg(args, query, "field", "--field")
        self.add_query_arg(args, query, "min", "--min")
        self.add_query_arg(args, query, "max", "--max")
        self.add_query_arg(args, query, "operation_filter", "--operation-filter")
        self.add_query_arg(args, query, "operationFilter", "--operation-filter")

        payload, status = run_c_command(args)
        if status == 200:
            payload["input"] = version_item["path"]
            payload["version"] = version_item["id"]
            payload["view"] = view
            payload["version_info"] = version_item
            payload["operation_summary"] = operation_counts(load_operation_marks(directory, metadata))
        self.send_json(payload, status)

    @staticmethod
    def add_query_arg(args, query, key, flag):
        value = query.get(key, [None])[0]
        if value not in (None, ""):
            args.extend([flag, value])


def main():
    ensure_workspace()
    host = "127.0.0.1"
    port = 8000
    server = ThreadingHTTPServer((host, port), WaterQualityHandler)
    print(f"Water quality UI: http://{host}:{port}")
    server.serve_forever()


if __name__ == "__main__":
    main()
