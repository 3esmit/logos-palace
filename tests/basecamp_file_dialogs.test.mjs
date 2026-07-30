import assert from "node:assert/strict";
import test from "node:test";

import {
  newlyOpenedFileDialogId,
  uniqueFileDialogIds,
} from "./basecamp_file_dialogs.mjs";

test("normalizes duplicate inspector entries for one file dialog", () => {
  const prior = uniqueFileDialogIds([{ id: "existing" }]);
  const current = uniqueFileDialogIds([
    { id: "existing" },
    { id: "picker" },
    { id: "picker" },
  ]);

  assert.deepEqual([...current], ["existing", "picker"]);
  assert.equal(newlyOpenedFileDialogId(current, prior), "picker");
});

test("rejects two distinct newly opened file dialogs", () => {
  const current = uniqueFileDialogIds([{ id: "one" }, { id: "two" }]);

  assert.throws(
    () => newlyOpenedFileDialogId(current, new Set()),
    /asset picker opened multiple dialogs/,
  );
});

test("rejects invalid file dialog identities", () => {
  assert.throws(
    () => uniqueFileDialogIds([{ id: "" }]),
    /asset picker dialog identity is invalid/,
  );
  assert.throws(
    () => uniqueFileDialogIds([{ id: "a".repeat(513) }]),
    /asset picker dialog identity is invalid/,
  );
});
