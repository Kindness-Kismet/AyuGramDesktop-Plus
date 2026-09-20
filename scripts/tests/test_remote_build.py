import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import sys


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from remote_build import (
    GitHubApi,
    RemoteBuildError,
    RemoteBuildInterrupted,
    TransientGitHubApiError,
    _API_VERSION,
    _append_dispatched_outputs,
    _append_dispatched_summary,
    _append_success_outputs,
    _append_success_summary,
    cancel_remote_run,
    run_remote_build,
)


REPOSITORY = "Kindness-Net/AyuGramDesktop-Plus-Windows-Build"
WORKFLOW = "build.yml"
REF = "main"


def run_payload(
    *,
    run_id=1234,
    attempt=1,
    status="completed",
    conclusion="success",
    repository=REPOSITORY,
):
    return {
        "id": run_id,
        "run_attempt": attempt,
        "status": status,
        "conclusion": conclusion,
        "repository": {"full_name": repository},
        "event": "workflow_dispatch",
        "path": ".github/workflows/build.yml",
        "head_branch": REF,
    }


class FakeApi:
    def __init__(self, responses, run_id=1234):
        self.responses = list(responses)
        self.last_response = self.responses[-1]
        self.run_id = run_id
        self.dispatches = []
        self.cancellations = []

    def dispatch_workflow(self, repository, workflow, ref, inputs):
        self.dispatches.append((repository, workflow, ref, dict(inputs)))
        return self.run_id

    def get_run(self, repository, run_id):
        if self.responses:
            self.last_response = self.responses.pop(0)
        return self.last_response

    def cancel_run(self, repository, run_id):
        self.cancellations.append((repository, run_id))


class FakeClock:
    def __init__(self):
        self.value = 0.0

    def monotonic(self):
        return self.value

    def sleep(self, seconds):
        self.value += seconds


class DispatchTests(unittest.TestCase):
    def test_api_uses_version_that_returns_dispatch_details(self):
        class Response:
            status = 200

            def __enter__(self):
                return self

            def __exit__(self, *_args):
                return False

            def read(self):
                return b"{}"

        api = GitHubApi("token")
        with patch("remote_build.urlopen", return_value=Response()) as open_url:
            api._request("GET", "/example")

        request = open_url.call_args.args[0]
        self.assertEqual(
            request.get_header("X-github-api-version"),
            _API_VERSION,
        )

    def test_dispatch_requires_returned_run_id(self):
        api = GitHubApi("token")
        with patch.object(
            api,
            "_request",
            return_value={"workflow_run_id": 9876},
        ) as request:
            run_id = api.dispatch_workflow(
                REPOSITORY,
                WORKFLOW,
                REF,
                {"source_sha": "a" * 40},
            )

        self.assertEqual(run_id, 9876)
        payload = request.call_args.kwargs["payload"]
        self.assertIs(payload["return_run_details"], True)
        self.assertEqual(payload["ref"], REF)
        self.assertEqual(payload["inputs"], {"source_sha": "a" * 40})

    def test_dispatch_never_guesses_when_id_is_missing(self):
        api = GitHubApi("token")
        with patch.object(api, "_request", return_value={}):
            with self.assertRaisesRegex(RemoteBuildError, "拒绝查询最新运行"):
                api.dispatch_workflow(REPOSITORY, WORKFLOW, REF, {})

    def test_get_run_retries_only_transient_read_failures(self):
        delays = []
        api = GitHubApi("token", sleep=delays.append)
        with patch.object(
            api,
            "_request",
            side_effect=[
                TransientGitHubApiError("first"),
                TransientGitHubApiError("second"),
                {"id": 9876},
            ],
        ) as request:
            result = api.get_run(REPOSITORY, 9876)

        self.assertEqual(result, {"id": 9876})
        self.assertEqual(delays, [1, 2])
        self.assertEqual(request.call_count, 3)

    def test_dispatch_post_is_not_retried(self):
        api = GitHubApi("token")
        with patch.object(
            api,
            "_request",
            side_effect=TransientGitHubApiError("temporary"),
        ) as request:
            with self.assertRaisesRegex(TransientGitHubApiError, "temporary"):
                api.dispatch_workflow(REPOSITORY, WORKFLOW, REF, {})
        self.assertEqual(request.call_count, 1)

    def test_socket_timeout_is_transient_but_dispatch_is_not_retried(self):
        api = GitHubApi("token")
        with patch("remote_build.urlopen", side_effect=TimeoutError("timed out")) as open_url:
            with self.assertRaisesRegex(TransientGitHubApiError, "timed out"):
                api.dispatch_workflow(REPOSITORY, WORKFLOW, REF, {})
        self.assertEqual(open_url.call_count, 1)

    def test_get_run_stops_after_three_transient_failures(self):
        delays = []
        api = GitHubApi("token", sleep=delays.append)
        with patch.object(
            api,
            "_request",
            side_effect=TransientGitHubApiError("temporary"),
        ) as request:
            with self.assertRaisesRegex(TransientGitHubApiError, "temporary"):
                api.get_run(REPOSITORY, 9876)
        self.assertEqual(delays, [1, 2])
        self.assertEqual(request.call_count, 3)


class RemoteBuildTests(unittest.TestCase):
    def run_build(self, api, clock=None, **overrides):
        clock = clock or FakeClock()
        arguments = {
            "repository": REPOSITORY,
            "workflow": WORKFLOW,
            "ref": REF,
            "inputs": {"source_sha": "a" * 40},
            "timeout_seconds": 60,
            "poll_interval_seconds": 5,
            "sleep": clock.sleep,
            "monotonic": clock.monotonic,
        }
        arguments.update(overrides)
        return run_remote_build(api, **arguments)

    def test_success_returns_exact_dispatch_run(self):
        api = FakeApi([run_payload()])
        details = self.run_build(api)

        self.assertEqual(details.run_id, 1234)
        self.assertEqual(details.run_attempt, 1)
        self.assertEqual(api.cancellations, [])
        self.assertEqual(api.dispatches[0][:3], (REPOSITORY, WORKFLOW, REF))

    def test_rerun_attempt_is_recorded_from_terminal_payload(self):
        api = FakeApi(
            [
                run_payload(status="in_progress", conclusion=None),
                run_payload(attempt=2, status="in_progress", conclusion=None),
                run_payload(attempt=2),
            ]
        )
        details = self.run_build(api)

        self.assertEqual(details.run_attempt, 2)
        self.assertEqual(api.cancellations, [])

    def test_failed_run_propagates_without_canceling_terminal_run(self):
        api = FakeApi([run_payload(conclusion="failure")])
        with self.assertRaisesRegex(RemoteBuildError, "远程构建失败"):
            self.run_build(api)
        self.assertEqual(api.cancellations, [])

    def test_timeout_cancels_exact_dispatched_run(self):
        api = FakeApi([run_payload(status="queued", conclusion=None)])
        with self.assertRaisesRegex(RemoteBuildError, "超时"):
            self.run_build(
                api,
                timeout_seconds=5,
                poll_interval_seconds=3,
            )
        self.assertEqual(api.cancellations, [(REPOSITORY, 1234)])

    def test_interruption_cancels_exact_dispatched_run(self):
        api = FakeApi([run_payload(status="queued", conclusion=None)])

        def interrupt(_seconds):
            raise RemoteBuildInterrupted("cancel")

        with self.assertRaisesRegex(RemoteBuildInterrupted, "cancel"):
            self.run_build(api, sleep=interrupt)
        self.assertEqual(api.cancellations, [(REPOSITORY, 1234)])

    def test_dispatched_output_exists_before_interrupted_wait(self):
        api = FakeApi([run_payload(status="queued", conclusion=None)])

        def interrupt(_seconds):
            raise RemoteBuildInterrupted("cancel")

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "output"
            with self.assertRaisesRegex(RemoteBuildInterrupted, "cancel"):
                self.run_build(
                    api,
                    sleep=interrupt,
                    on_dispatched=lambda repository, run_id, url: (
                        _append_dispatched_outputs(
                            output,
                            repository,
                            run_id,
                            url,
                        )
                    ),
                )
            text = output.read_text(encoding="utf-8")
        self.assertIn("run_id=1234\n", text)
        self.assertNotIn("run_attempt=", text)
        self.assertEqual(api.cancellations, [(REPOSITORY, 1234)])

    def test_cancel_failure_does_not_replace_timeout(self):
        api = FakeApi([run_payload(status="queued", conclusion=None)])

        def fail_cancel(_repository, _run_id):
            raise RemoteBuildError("cancel failed")

        api.cancel_run = fail_cancel
        with self.assertRaisesRegex(RemoteBuildError, "超时"):
            self.run_build(
                api,
                timeout_seconds=1,
                poll_interval_seconds=1,
            )

    def test_exact_cancel_helper_never_discovers_another_run(self):
        api = FakeApi([run_payload()])
        self.assertTrue(cancel_remote_run(api, REPOSITORY, 4321))
        self.assertEqual(api.cancellations, [(REPOSITORY, 4321)])

    def test_mismatched_run_metadata_is_rejected_and_canceled(self):
        api = FakeApi(
            [run_payload(repository="Kindness-Net/Another-Build")]
        )
        with self.assertRaisesRegex(RemoteBuildError, "仓库不一致"):
            self.run_build(api)
        self.assertEqual(api.cancellations, [(REPOSITORY, 1234)])

    def test_outputs_and_summary_keep_run_id_attempt_and_url(self):
        api = FakeApi([run_payload(attempt=2)])
        details = self.run_build(api)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "output"
            summary = Path(directory) / "summary"
            _append_dispatched_outputs(
                output,
                details.repository,
                details.run_id,
                details.url,
            )
            _append_success_outputs(output, details)
            _append_dispatched_summary(
                summary,
                "Windows",
                details.repository,
                details.run_id,
                details.url,
            )
            _append_success_summary(summary, details)

            output_text = output.read_text(encoding="utf-8")
            summary_text = summary.read_text(encoding="utf-8")
            self.assertIn("run_id=1234\n", output_text)
            self.assertIn("run_attempt=2\n", output_text)
            self.assertIn("actions/runs/1234", summary_text)
            self.assertIn("Result: success (attempt 2)", summary_text)

    def test_dispatch_link_is_available_before_failure(self):
        api = FakeApi([run_payload(conclusion="failure")])
        links = []
        with self.assertRaisesRegex(RemoteBuildError, "远程构建失败"):
            self.run_build(
                api,
                on_dispatched=lambda repository, run_id, url: links.append(
                    (repository, run_id, url)
                ),
            )
        self.assertEqual(links[0][:2], (REPOSITORY, 1234))
        self.assertTrue(links[0][2].endswith("/actions/runs/1234"))


if __name__ == "__main__":
    unittest.main()
