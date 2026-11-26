# Optional Windows ASR packaging (M1 phase 1)

`SUBREC_ASR_WITH_SHERPA` stays **OFF by default**. These Python standard-library
scripts are not wired into default CMake. Build the independent x64 Release host
with **MSVC and static CRT `/MT` (D12)**, using the C API header and import library
from the full sherpa-onnx v2.0.2 shared-MT no-tts Windows package. Do not use the
MinGW application build to link that package. The separate `asr-msvc` CI job builds
the ASR bundle; existing payloads without `asr/` remain valid.

`manifest.json` records the verified URLs, sizes, SHA256 values, licenses, package
layout, PE dependency observations and exclusions. Its v6.2.3 file SHA256 is not
the git blob SHA. **VAD v6.2.3 is selected as the installer default: S0 passed
2026-11-26 (decision 10).** v4-series is an A/B control only;
do not follow v6.3/master. SenseVoice is NOT shipped or fetched by these scripts;
ModelManager downloads it after FunASR Model License 1.1 acceptance.

## Verify / fetch (never store downloaded assets in the repository)

Offline verification of the already downloaded files:

```sh
python3 packaging/asr/fetch.py --cache /workspace/projects/pkgC-verify-win --asset sherpa-windows --asset vad-v4-sherpa --asset vad-v6.2.3
```

`--asset` is required and repeatable, with only those three pinned choices. No
network occurs by default. Adding `--download`
fetches missing selected files from the pinned URLs into temporary files, verifies
size and SHA256, then atomically publishes them. A cached mismatch exits nonzero
and is never silently replaced. No extraction or license download is performed.
The `asr-msvc` CI job uses `--download` for `sherpa-windows` and `vad-v6.2.3`,
reusing the verified files in its cache outside the repository.

## Assemble

First verify the archive, then use its unmodified extraction as `SHERPA_ONNX_ROOT`.
The package contains `include/sherpa-onnx/c-api/c-api.h`; no fallback is needed.
Given a separately built MSVC `/MT` host, for example in PowerShell:

```powershell
$env:SHERPA_ONNX_ROOT = 'D:\deps\sherpa-onnx-v2.0.2-win-x64-shared-MT-Release-no-tts'
python packaging/asr/assemble.py --exe D:\build-asr\subrec-asr.exe --payload D:\payload
```

`--sherpa-onnx-root` can replace the environment variable. The output is:

```text
asr/subrec-asr.exe
asr/sherpa-onnx-c-api.dll
asr/onnxruntime.dll
asr/onnxruntime_providers_shared.dll
licenses/asr/{sherpa-onnx,onnxruntime,silero-vad}.LICENSE
licenses/asr/NOTICE.txt
```

The three DLLs come exclusively from `lib/`. `onnxruntime_providers_shared.dll`
is included because it is small and in the official package, even though it is
not an onnxruntime load-time import. The C++ API DLL, import libraries, headers
and all sherpa CLI executables are excluded. All three original license texts
are SHA256-checked before copying, including Silero's license when VAD is absent.
Archive integrity is checked by `fetch.py`; assembly checks DLL names/sizes and
requires the full-package header. It does not authenticate a modified extraction
or prove the supplied executable's toolchain/engine configuration.

For release/CI assembly with the selected VAD, explicitly pass both flags:

```powershell
python packaging/asr/assemble.py --exe D:\build-asr\subrec-asr.exe --payload D:\payload --vad v6.2.3 --vad-file D:\deps\vad\silero_vad_v6.2.3.onnx
```

Assembly verifies the model's size and SHA256 before copying it as
`asr/silero_vad.onnx`. **Without `--vad`/`--vad-file`, assembly still omits VAD;
a release may ship without VAD.** v4 is never accepted by `assemble.py`.
CI includes v6.2.3 in `subrec-asr-win64`, but not in `subrec-asr-tests-win64`.
For PC3, copy `asr/silero_vad.onnx` from `subrec-asr-win64` beside the test/host
executables, or set `SUBREC_ASR_VAD_MODEL` to the model's full path.

Assembly refuses existing `asr/` or `licenses/asr/` destinations, so a rerun cannot
silently retain a stale VAD, DLL or CLI. Use fresh ASR destinations within the
payload. Other payload directories may already exist. Never commit payload binaries.
`package.py prepare` allows ASR absence; when present, it requires the complete
runtime and license set, rejects extra ASR files, verifies license hashes and
checks any VAD against the pinned filename/hash. I8 cleans both explicit ASR
file patterns and the registered whole `asr` directory during upgrades.

Offline regression checks:

```sh
python3 packaging/asr/test_asr.py
python3 packaging/installer/test_package.py
```
