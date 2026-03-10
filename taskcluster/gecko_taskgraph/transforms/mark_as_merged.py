# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.schema import resolve_keyed_by

from gecko_taskgraph.util.attributes import release_level

transforms = TransformSequence()


@transforms.add
def make_task_description(config, jobs):
    merge_config = config.params.get("merge_config", {})
    merge_automation_id = merge_config.get("merge-automation-id")

    if not merge_automation_id:
        return

    for job in jobs:
        resolve_keyed_by(
            job,
            "scopes",
            item_name=job["name"],
            **{"release-level": release_level(config.params["project"])},
        )
        resolve_keyed_by(
            job,
            "worker-type",
            item_name=job["name"],
            **{"release-level": release_level(config.params["project"])},
        )

        job["worker"]["merge-automation-id"] = merge_automation_id
        job.setdefault("dependencies", {})["merge-automation"] = "merge-{}".format(
            merge_config["behavior"]
        )
        job.pop("task-from", None)

        yield job
