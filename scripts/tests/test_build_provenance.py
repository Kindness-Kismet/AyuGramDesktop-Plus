import io
import json
import sys
import tempfile
import unittest
import urllib.error
from email.message import Message
from pathlib import Path
from unittest.mock import patch


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from build_provenance import (
    SOURCE_REPOSITORY,
    TARGETS,
    ProvenanceError,
    fetch_workflow_run,
    validate_source,
    verify_artifacts,
    write_artifact_manifest,
)


SOURCE_SHA = "0123456789abcdef0123456789abcdef01234567"
SOURCE_REF = "refs/heads/release-test"
VERSION = "7.2.9.1"
APP_UPDATE_VERSION = 70_200_901
SOURCE_RUN_ID = 1001
SOURCE_RUN_ATTEMPT = 2


class FakeResponse(io.BytesIO):
    def __init__(self, payload):
        super().__init__(json.dumps(payload).encode("utf-8"))


def http_error(status, **headers):
    message = Message()
    for name, value in headers.items():
        message[name.replace("_", "-")] = str(value)
    return urllib.error.HTTPError(
        "https://api.github.com/redacted",
        status,
        "request failed",
        message,
        io.BytesIO(b'{"message":"response body must stay private"}'),
    )


class SourceApiTests(unittest.TestCase):
    def opener(self, responses, calls):
        responses = iter(responses)

        def open_request(_request, *, timeout):
            calls.append(timeout)
            response = next(responses)
            if isinstance(response, BaseException):
                raise response
            return response

        return open_request

    def test_transient_http_and_network_failures_retry_then_succeed(self):
        calls = []
        sleeps = []
        payload = {"id": SOURCE_RUN_ID}
        result = fetch_workflow_run(
            SOURCE_REPOSITORY,
            SOURCE_RUN_ID,
            SOURCE_RUN_ATTEMPT,
            opener=self.opener([http_error(503), TimeoutError(), FakeResponse(payload)], calls),
            sleep=sleeps.append,
        )
        self.assertEqual(result, payload)
        self.assertEqual(calls, [15, 15, 15])
        self.assertEqual(sleeps, [1.0, 2.0])

    def test_anonymous_rate_limit_403_has_bounded_retries_and_diagnostics(self):
        calls = []
        sleeps = []
        errors = [
            http_error(
                403,
                X_GitHub_Request_Id="request-123",
                X_RateLimit_Remaining="0",
                X_RateLimit_Reset="1234567890",
                X_RateLimit_Resource="core",
            )
            for _ in range(4)
        ]
        with self.assertRaises(ProvenanceError) as raised:
            fetch_workflow_run(
                SOURCE_REPOSITORY,
                SOURCE_RUN_ID,
                SOURCE_RUN_ATTEMPT,
                opener=self.opener(errors, calls),
                sleep=sleeps.append,
            )
        message = str(raised.exception)
        self.assertIn("HTTP 403", message)
        self.assertIn("request_id=request-123", message)
        self.assertIn("rate_limit_remaining=0", message)
        self.assertIn("rate_limit_reset=1234567890", message)
        self.assertIn("尝试 4/4", message)
        self.assertEqual(sleeps, [1.0, 2.0, 4.0])

    def test_non_transient_http_error_fails_without_retry_and_redacts_secrets(self):
        calls = []
        secret = "never-print-this-token"
        error = http_error(404, X_GitHub_Request_Id="request-404")
        with patch.dict("os.environ", {"GH_TOKEN": secret}, clear=False):
            with self.assertRaises(ProvenanceError) as raised:
                fetch_workflow_run(
                    SOURCE_REPOSITORY,
                    SOURCE_RUN_ID,
                    SOURCE_RUN_ATTEMPT,
                    opener=self.opener([error], calls),
                    sleep=lambda _: self.fail("404 must not retry"),
                )
        message = str(raised.exception)
        self.assertIn("HTTP 404", message)
        self.assertIn("request_id=request-404", message)
        self.assertNotIn(secret, message)
        self.assertNotIn("response body", message)
        self.assertEqual(calls, [15])

    def test_rate_limit_retry_after_is_capped(self):
        calls = []
        sleeps = []
        payload = {"id": SOURCE_RUN_ID}
        result = fetch_workflow_run(
            SOURCE_REPOSITORY,
            SOURCE_RUN_ID,
            SOURCE_RUN_ATTEMPT,
            opener=self.opener(
                [http_error(429, Retry_After="600"), FakeResponse(payload)],
                calls,
            ),
            sleep=sleeps.append,
        )
        self.assertEqual(result, payload)
        self.assertEqual(sleeps, [30])


class SourceValidationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        version_file = self.root / "Telegram/build/version"
        version_file.parent.mkdir(parents=True)
        version_file.write_text(
            "\n".join(
                (
                    "AppVersionStrSmall 7.2.9.1",
                    "AppVersionStr 7.2.9.1",
                    "AppVersionOriginal 7.2.9.1",
                    "AppUpdateVersion 70200901",
                )
            )
            + "\n",
            encoding="utf-8",
        )

    def run_payload(self, **overrides):
        payload = {
            "id": SOURCE_RUN_ID,
            "run_attempt": SOURCE_RUN_ATTEMPT,
            "repository": {"full_name": SOURCE_REPOSITORY},
            "head_sha": SOURCE_SHA,
            "head_branch": "release-test",
            "path": ".github/workflows/release.yml",
            "event": "workflow_dispatch",
            "actor": {"login": "KiritoXDone"},
        }
        payload.update(overrides)
        return payload

    def validate(self, loader):
        with patch("build_provenance._git_head", return_value=SOURCE_SHA):
            validate_source(
                self.root,
                repository=SOURCE_REPOSITORY,
                sha=SOURCE_SHA,
                ref=SOURCE_REF,
                run_id=SOURCE_RUN_ID,
                run_attempt=SOURCE_RUN_ATTEMPT,
                version=VERSION,
                appupdateversion=APP_UPDATE_VERSION,
                workflow_path=".github/workflows/release.yml",
                run_loader=loader,
            )

    def test_accepts_matching_manual_run(self):
        def loader(repository, run_id, run_attempt):
            self.assertEqual((repository, run_id, run_attempt), (SOURCE_REPOSITORY, 1001, 2))
            return self.run_payload()

        self.validate(loader)

    def test_actor_does_not_affect_source_validation(self):
        for actor in ({"login": "Kindness-Kismet"}, None):
            with self.subTest(actor=actor):
                payload = self.run_payload()
                if actor is None:
                    payload.pop("actor")
                else:
                    payload["actor"] = actor
                self.validate(lambda *_: payload)

    def test_rejects_pull_request_source_run(self):
        with self.assertRaisesRegex(ProvenanceError, "事件"):
            self.validate(lambda *_: self.run_payload(event="pull_request"))

    def test_rejects_wrong_source_run_attempt(self):
        calls = []

        def loader(*_):
            calls.append(1)
            return self.run_payload(run_attempt=1)

        with self.assertRaisesRegex(ProvenanceError, "run attempt"):
            self.validate(loader)
        self.assertEqual(calls, [1])

    def test_api_failure_fails_closed(self):
        def loader(*_):
            raise ProvenanceError("GitHub API 无法确认 source workflow run")

        with self.assertRaisesRegex(ProvenanceError, "无法确认"):
            self.validate(loader)

    def test_rejects_checkout_with_wrong_sha(self):
        with patch("build_provenance._git_head", return_value="f" * 40):
            with self.assertRaisesRegex(ProvenanceError, "checkout HEAD"):
                validate_source(
                    self.root,
                    repository=SOURCE_REPOSITORY,
                    sha=SOURCE_SHA,
                    ref=SOURCE_REF,
                    run_id=SOURCE_RUN_ID,
                    run_attempt=SOURCE_RUN_ATTEMPT,
                    version=VERSION,
                    appupdateversion=APP_UPDATE_VERSION,
                    workflow_path=".github/workflows/release.yml",
                    run_loader=lambda *_: self.run_payload(),
                )


class ArtifactProvenanceTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.build_runs = {}

    def add_target(self, key, run_id=None, run_attempt=1):
        platform, arch = key.split("-", 1)
        target = TARGETS[key]
        run_id = run_id or 2000 + len(self.build_runs)
        directory = self.root / platform
        directory.mkdir(exist_ok=True)
        archive = directory / f"AyuGram-v{VERSION}-{target['archive_platform']}-{arch}.zip"
        archive.write_bytes(f"archive-{key}".encode())
        updaters = []
        for prefix in target["updater_prefixes"]:
            updater = directory / f"{prefix}{APP_UPDATE_VERSION}"
            updater.write_bytes(f"updater-{key}-{prefix}".encode())
            updaters.append(updater)
        output = directory / f"provenance-{platform}-{arch}.json"
        write_artifact_manifest(
            platform=platform,
            arch=arch,
            source_repository=SOURCE_REPOSITORY,
            source_ref=SOURCE_REF,
            source_sha=SOURCE_SHA,
            source_run_id=SOURCE_RUN_ID,
            source_run_attempt=SOURCE_RUN_ATTEMPT,
            builder_repository=target["repository"],
            builder_run_id=run_id,
            builder_run_attempt=run_attempt,
            version=VERSION,
            appupdateversion=APP_UPDATE_VERSION,
            output=output,
            files=[archive, *updaters],
        )
        self.build_runs[key] = {
            "repository": target["repository"],
            "run_id": run_id,
            "run_attempt": run_attempt,
        }
        return output

    def add_required_targets(self):
        for key, target in TARGETS.items():
            if target["required"]:
                self.add_target(key)

    def verify(self, **overrides):
        arguments = {
            "source_repository": SOURCE_REPOSITORY,
            "source_ref": SOURCE_REF,
            "source_sha": SOURCE_SHA,
            "source_run_id": SOURCE_RUN_ID,
            "source_run_attempt": SOURCE_RUN_ATTEMPT,
            "version": VERSION,
            "appupdateversion": APP_UPDATE_VERSION,
            "build_runs_json": json.dumps(self.build_runs),
        }
        arguments.update(overrides)
        verify_artifacts(self.root, **arguments)

    def rewrite_manifest(self, path, mutate):
        data = json.loads(path.read_text(encoding="utf-8"))
        mutate(data)
        path.write_text(json.dumps(data), encoding="utf-8")

    def test_required_targets_pass_without_optional_architectures(self):
        self.add_required_targets()
        self.verify()

    def test_declared_optional_architecture_is_verified(self):
        self.add_required_targets()
        self.add_target("windows-arm64")
        self.add_target("linux-arm64")
        self.verify()

    def test_missing_required_architecture_is_rejected(self):
        self.add_required_targets()
        self.build_runs.pop("macos-universal")
        with self.assertRaisesRegex(ProvenanceError, "缺少必需目标.*macos-universal"):
            self.verify()

    def test_universal_macos_manifest_covers_both_update_channels(self):
        self.add_required_targets()
        path = self.root / "macos/provenance-macos-universal.json"
        manifest = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(
            {entry["name"] for entry in manifest["files"]},
            {
                f"AyuGram-v{VERSION}-macos-universal.zip",
                f"tmacupd{APP_UPDATE_VERSION}",
                f"tarmacupd{APP_UPDATE_VERSION}",
            },
        )

    def test_wrong_source_sha_is_rejected(self):
        self.add_required_targets()
        path = self.root / "windows/provenance-windows-x64.json"
        self.rewrite_manifest(path, lambda data: data["source"].update(sha="f" * 40))
        with self.assertRaisesRegex(ProvenanceError, "source sha"):
            self.verify()

    def test_wrong_source_ref_is_rejected(self):
        self.add_required_targets()
        path = self.root / "windows/provenance-windows-x64.json"
        self.rewrite_manifest(path, lambda data: data["source"].update(ref="refs/heads/other"))
        with self.assertRaisesRegex(ProvenanceError, "source ref"):
            self.verify()

    def test_wrong_builder_run_attempt_is_rejected(self):
        self.add_required_targets()
        path = self.root / "linux/provenance-linux-x64.json"
        self.rewrite_manifest(path, lambda data: data["builder"].update(run_attempt=9))
        with self.assertRaisesRegex(ProvenanceError, "builder run"):
            self.verify()

    def test_modified_artifact_hash_is_rejected(self):
        self.add_required_targets()
        archive = self.root / f"windows/AyuGram-v{VERSION}-win-x64.zip"
        archive.write_bytes(b"different-length-payload")
        with self.assertRaisesRegex(ProvenanceError, "大小|SHA-256"):
            self.verify()

    def test_manifest_path_traversal_is_rejected(self):
        self.add_required_targets()
        path = self.root / "macos/provenance-macos-universal.json"
        self.rewrite_manifest(path, lambda data: data["files"][0].update(name="../outside.zip"))
        with self.assertRaisesRegex(ProvenanceError, "不能包含路径"):
            self.verify()

    def test_unexpected_file_is_rejected(self):
        self.add_required_targets()
        (self.root / "windows/unsigned.zip").write_bytes(b"unsigned")
        with self.assertRaisesRegex(ProvenanceError, "未声明文件"):
            self.verify()

    def test_duplicate_filename_across_downloads_is_rejected(self):
        self.add_required_targets()
        duplicate = self.root / "duplicate" / f"tx64upd{APP_UPDATE_VERSION}"
        duplicate.parent.mkdir()
        duplicate.write_bytes(b"duplicate")
        with self.assertRaisesRegex(ProvenanceError, "文件名重复"):
            self.verify()

    def test_writer_rejects_wrong_platform_repository(self):
        directory = self.root / "windows"
        directory.mkdir()
        archive = directory / f"AyuGram-v{VERSION}-win-x64.zip"
        updater = directory / f"tx64upd{APP_UPDATE_VERSION}"
        archive.write_bytes(b"archive")
        updater.write_bytes(b"updater")
        with self.assertRaisesRegex(ProvenanceError, "必须由"):
            write_artifact_manifest(
                platform="windows",
                arch="x64",
                source_repository=SOURCE_REPOSITORY,
                source_ref=SOURCE_REF,
                source_sha=SOURCE_SHA,
                source_run_id=SOURCE_RUN_ID,
                source_run_attempt=SOURCE_RUN_ATTEMPT,
                builder_repository=TARGETS["linux-x64"]["repository"],
                builder_run_id=2000,
                builder_run_attempt=1,
                version=VERSION,
                appupdateversion=APP_UPDATE_VERSION,
                output=directory / "provenance-windows-x64.json",
                files=[archive, updater],
            )


if __name__ == "__main__":
    unittest.main()
