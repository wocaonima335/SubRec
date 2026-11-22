#pragma once
// tests/subtitle_models：工作包 E 验收用例（docs/subtitle/work-packages.md §3 表 E）。
// 单个 Qt Test 类、每用例一个 slot，ctest 按函数名单独运行（见本目录 CMakeLists.txt）。
// 全部网络交互走 127.0.0.1 的 TestHttpServer，绝不联网；无需显示（QTEST_GUILESS_MAIN）。

#include <QtTest>

class SubtitleModelsTests : public QObject {
    Q_OBJECT

private slots:
    // ---- 内置清单 ----
    void manifest_builtin_selfcheck();

    // ---- 反射约定（ui/backend/model_catalog.cpp）----
    void reflection_contract();
    void models_keys_match_catalog_roles();

    // ---- 下载器：续传/断线/哈希/镜像切换/大小校验/协议限制/多文件/取消 ----
    void range_resume_from_part();
    void disconnect_switches_mirror();
    void hash_mismatch_switches_mirror();
    void hash_mismatch_all_fail();
    void mirror_404_then_success();
    void mirror_refused_then_success();
    void server_ignores_range_restarts();
    void size_mismatch_rejected();
    void http_scheme_rejected_without_loopback();
    void multi_file_download();
    void cancel_keeps_part();

    // ---- 许可确认 ----
    void license_gate_blocks_and_accepts();
    void license_version_change_requires_reaccept();
    void license_persists_across_instances();

    // ---- 删除 / stale / engineRequestFor ----
    void remove_installed();
    void remove_while_downloading();
    void stale_detection();
    void engine_request_installed();
    void engine_request_missing_or_stale();
};
