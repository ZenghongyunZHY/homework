# 海水养殖水质数据分析系统

## 项目简介

本项目使用 C 语言实现海水养殖水质数据分析内核，处理水温、盐度、pH、溶解氧、降水量、气温 6 项参数，支持数据读取、预处理、统计分析、风险预警、线性回归预测和数据维护。

项目同时提供一个轻量本地 Web 界面：前端使用原生 HTML/CSS/JS，服务层使用 Python 标准库 `http.server`，所有数据清洗、分析、预测和增删改仍由 C 可执行程序完成。

## 当前工作流

程序启动后工作区默认为空，不会自动读取 `dao/` 目录中的数据。管理员需要先在“添加文件”中上传 CSV 文件，系统随后自动执行预处理。

每个上传文件存放在：

```text
workspace/files/<file_id>/
```

核心文件结构：

```text
raw.csv
versions/v000_raw.csv
versions/v001_preprocessed.csv
preview/raw_preprocess_marks.csv
preview/processed_operation_marks.json
metadata.json
logs.jsonl
```

说明：

- `v000_raw.csv`：未处理原始版本，只用于展示预处理预测标记。
- `v001_preprocessed.csv`：已处理工作副本，修改、软删除、新增都直接作用在这个文件上。
- `raw_preprocess_marks.csv`：原始版本的预处理标记，用于显示“不处理、将删除、缺失填充、异常修复、修复并填充”。
- `processed_operation_marks.json`：已处理版本的操作标记，用于显示“已修改、已删除、新增”。
- 不再为每次修改或删除生成 `v002_modify.csv`、`v003_delete.csv` 这类快照版本。
- 删除采用软删除：记录仍保留在表格中，但统计、预警、预测默认排除软删除记录。

## 运行方式

1. 编译 C 数据处理程序：

```bash
gcc -std=c11 -Wall -Wextra -I. main.c controller/DataController.c service/DataService.c view/DataView.c util/operation_file.c -lm -o water_quality.exe
```

2. 启动本地 Web 服务：

```bash
python server.py
```

3. 浏览器访问：

```text
http://127.0.0.1:8000
```

默认账号：

- 管理员：`admin / 123456`
- 访客：`guest / guest`

## Web 功能

- 添加文件：上传 CSV，并自动生成未处理版本、已处理版本和预处理标记。
- 已保存文件：查看所有上传文件、核心版本和操作数量摘要。
- 数据浏览：拆分为“未处理版本文件”和“已处理版本文件”。
- 未处理版本文件：按预处理方式筛选并用底色标记将删除、缺失填充、异常修复等数据。
- 已处理版本文件：按操作方式筛选并用底色标记已修改、已删除、新增记录；被修改的具体字段会有更深底色。
- 数据维护：在已处理版本中修改字段、软删除记录、新增完整记录。
- 数据预处理：查看预处理日志，也可重新预处理原始版本。重新预处理会覆盖 `v001_preprocessed.csv` 并重置操作标记。
- 日志：按文件分组展示上传、预处理、修改、软删除、新增日志，记录操作者、时间、记录号、字段、旧值、新值。
- 统计分析：点击“刷新统计”后，对当前文件的 `v001_preprocessed.csv` 重新计算统计结果，并排除软删除记录。
- 预警报告、预测分析：均基于当前文件的已处理工作副本，并排除软删除记录。

## C CLI 示例

```bash
water_quality.exe preprocess --input raw.csv --output versions/v001_preprocessed.csv --marks preview/raw_preprocess_marks.csv
water_quality.exe query --input versions/v000_raw.csv --view raw --raw-marks preview/raw_preprocess_marks.csv
water_quality.exe query --input versions/v001_preprocessed.csv --view processed --op-marks preview/processed_operation_marks.json
water_quality.exe modify --input versions/v001_preprocessed.csv --row 10 --field ph --value 8.1
water_quality.exe delete --input versions/v001_preprocessed.csv --row 20
water_quality.exe add --input versions/v001_preprocessed.csv --temp 25 --salinity 34 --ph 8.1 --do 6 --precipitation 0 --air_temp 27
water_quality.exe stats --input versions/v001_preprocessed.csv --op-marks preview/processed_operation_marks.json
```

## 测试重点

- 上传后只生成 `v000_raw.csv` 和 `v001_preprocessed.csv` 两个核心版本。
- 修改字段后，已处理表格中对应记录和字段有操作标记，日志记录旧值和新值。
- 软删除后，记录仍显示在已处理表格中，但统计刷新时被排除。
- 新增记录会追加到已处理文件末尾，并显示为新增记录。
- 未处理版本和已处理版本使用不同的筛选项与标记逻辑。
