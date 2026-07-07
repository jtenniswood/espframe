from __future__ import annotations

from product_contract.common import ROOT, check_relative_path, read, require_contains


def check_pull_request_template_contract(project: dict, workflow_names: object, errors: list[str]) -> None:
    pull_request_template_path = check_relative_path(
        project.get("github_pull_request_template_path"),
        "project.github_pull_request_template_path",
        errors,
    )
    if not pull_request_template_path:
        return

    label = pull_request_template_path
    template = read(ROOT / pull_request_template_path, errors)
    for heading in ("## Automated checks", "## Device testing", "## Notes for reviewers"):
        require_contains(template, heading, label, errors)

    local_check_command = str(project.get("local_check_command", "")).strip()
    if local_check_command:
        require_contains(template, f"`{local_check_command}`", label, errors)

    compile_workflow_name = ""
    if isinstance(workflow_names, dict):
        compile_workflow_name = str(workflow_names.get("compile", "")).strip()
    if compile_workflow_name:
        require_contains(template, compile_workflow_name, label, errors)

    compile_artifact_prefix = str(project.get("compile_firmware_artifact_prefix", "")).strip()
    if compile_artifact_prefix:
        require_contains(template, compile_artifact_prefix, label, errors)

    for needle in ("workflow run/artifact", "Firmware artifact", "Device tested", "Result/notes"):
        require_contains(template, needle, label, errors)

    device_testing_options = project.get("github_pull_request_device_testing_options", [])
    if isinstance(device_testing_options, list):
        for option in device_testing_options:
            option_text = str(option).strip()
            if option_text:
                require_contains(template, f"- [ ] {option_text}", label, errors)
