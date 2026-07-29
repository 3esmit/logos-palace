#!/usr/bin/env node

import assert from "node:assert/strict";
import test from "node:test";
import {
  cleanupAttestedScope,
  cleanupDisposition,
  cleanupPlannedScope,
  parseCgroupEvents,
  parseUnifiedCgroup,
  releaseAttestedScopeBarrier,
  retireScopeSlice,
  scopeBarrierSignalArguments,
  validateCleanedScopeEvidence,
  validateControlGroups,
  validateScopeIdentity,
} from "./basecamp_scope.mjs";

const slice = "logos-palace-run-AB12cd34.slice";
const unit = "logos-palace-run-AB12cd34-gate2-Ef56Gh78.scope";
const sliceControlGroup =
  "/user.slice/user-1000.slice/user@1000.service/"
  + "logos-palace-run-AB12cd34.slice";
const controlGroup = `${sliceControlGroup}/${unit}`;

function evidence() {
  const cgroupRoot = "/sys/fs/cgroup";
  const cgroupPath = `${cgroupRoot}${controlGroup}`;
  return {
    schema: "logos.palace.basecamp-process-scope",
    version: 1,
    status: "attested",
    unit,
    slice,
    controlGroup,
    attestedPid: 1234,
    attestedStartTimeTicks: 5678,
    barrier: "sigstop-before-exec",
    cgroupPath,
    eventsPath: `${cgroupPath}/cgroup.events`,
    killPath: `${cgroupPath}/cgroup.kill`,
    sliceControlGroup,
    sliceCgroupPath: `${cgroupRoot}${sliceControlGroup}`,
    sliceEventsPath:
      `${cgroupRoot}${sliceControlGroup}/cgroup.events`,
    sliceKillPath:
      `${cgroupRoot}${sliceControlGroup}/cgroup.kill`,
  };
}

function cleanedEvidence() {
  return {
    ...evidence(),
    status: "cleaned",
    commandExitStatus: 0,
    cleanup: {
      status: "passed",
      initiallyPopulated: false,
      residueKilled: false,
      finalPopulated: false,
      sliceInitiallyPopulated: false,
      sliceResidueKilled: false,
      sliceFinalPopulated: false,
    },
  };
}

function retiredSliceOptions(overrides = {}) {
  return {
    systemctl: "/usr/bin/systemctl",
    controlGroupFor: async () => undefined,
    validateMount: async () => {},
    stopSlice: async () => {},
    activeStateFor: async () => "inactive",
    loadedStateFor: async () => false,
    ...overrides,
  };
}

function stoppedStat(pid, startTimeTicks) {
  const fields = Array(20).fill("0");
  fields[0] = "T";
  fields[19] = String(startTimeTicks);
  return Buffer.from(`${pid} (scope barrier) ${fields.join(" ")}\n`);
}

test("parses exactly one canonical unified cgroup-v2 path", () => {
  assert.equal(
    parseUnifiedCgroup(Buffer.from(`0::${controlGroup}\n`)),
    controlGroup,
  );
  for (const malformed of [
    "",
    "0::/\n",
    "2:cpu:/legacy\n",
    `0::${controlGroup}\n2:cpu:/legacy\n`,
    `0::${controlGroup}/../other\n`,
    `0::${controlGroup}//nested\n`,
    `0::${controlGroup}\0nested\n`,
    `0::${controlGroup}`,
  ]) {
    assert.throws(
      () => parseUnifiedCgroup(Buffer.from(malformed)),
      /cgroup|scope bounds/,
    );
  }
});

test("binds gate scope to exact run slice token", () => {
  assert.deepEqual(validateScopeIdentity({ unit, slice }), {
    prefix: "logos-palace-run-AB12cd34",
    runId: "AB12cd34",
    slice,
    unit,
  });
  for (const candidate of [
    {
      unit: "logos-palace-run-AB12cd34-gate5-Ef56Gh78.scope",
      slice,
    },
    {
      unit: "logos-palace-run-OTHER000-gate2-Ef56Gh78.scope",
      slice,
    },
    { unit: "logos-palace-run-AB12cd34-gate2.scope", slice },
    { unit, slice: "app.slice" },
    { unit: "../gate2.scope", slice },
  ]) {
    assert.throws(
      () => validateScopeIdentity(candidate),
      /scope .* name is invalid/,
    );
  }
});

test("requires PID scope to be direct child of exact run slice", () => {
  assert.equal(
    validateControlGroups({
      pidControlGroup: controlGroup,
      unitControlGroup: controlGroup,
      sliceControlGroup,
      unit,
      slice,
    }),
    controlGroup,
  );
  assert.throws(
    () =>
      validateControlGroups({
        pidControlGroup: `${controlGroup}/nested`,
        unitControlGroup: controlGroup,
        sliceControlGroup,
        unit,
        slice,
      }),
    /identities differ/,
  );
  assert.throws(
    () =>
      validateControlGroups({
        pidControlGroup: controlGroup,
        unitControlGroup: controlGroup,
        sliceControlGroup: dirnameForTest(sliceControlGroup),
        unit,
        slice,
      }),
    /identities differ/,
  );
});

test("accepts only exact residue-free cleaned scope evidence", () => {
  assert.equal(
    validateCleanedScopeEvidence(cleanedEvidence(), {
      gate: "gate2",
      slice,
    }).unit,
    unit,
  );
  for (const mutate of [
    (value) => {
      value.extra = true;
    },
    (value) => {
      value.cleanup.extra = true;
    },
    (value) => {
      value.cleanup.sliceResidueKilled = true;
    },
    (value) => {
      value.commandExitStatus = 7;
    },
    (value) => {
      value.controlGroup = `${value.sliceControlGroup}/other.scope`;
    },
    (value) => {
      value.sliceCgroupPath = "/sys/fs/cgroup/other.slice";
    },
  ]) {
    const candidate = structuredClone(cleanedEvidence());
    mutate(candidate);
    assert.throws(
      () =>
        validateCleanedScopeEvidence(candidate, {
          gate: "gate2",
          slice,
        }),
      /cleaned process scope|ControlGroup/,
    );
  }
  assert.throws(
    () =>
      validateCleanedScopeEvidence(cleanedEvidence(), {
        gate: "gate1",
        slice,
      }),
    /cleaned process scope/,
  );
});

test("cleanup disposition reports unit or slice residue", () => {
  assert.equal(cleanupDisposition(cleanedEvidence()), "clean");
  for (const field of ["residueKilled", "sliceResidueKilled"]) {
    const candidate = cleanedEvidence();
    candidate.cleanup[field] = true;
    assert.equal(cleanupDisposition(candidate), "residue-killed");
  }
  const malformed = cleanedEvidence();
  malformed.cleanup.unexpected = false;
  assert.throws(
    () => cleanupDisposition(malformed),
    /cleanup result is invalid/,
  );
});

function dirnameForTest(path) {
  return path.slice(0, path.lastIndexOf("/"));
}

test("parses bounded populated state", () => {
  assert.deepEqual(
    parseCgroupEvents("populated 1\nfrozen 0\n"),
    { populated: true },
  );
  assert.deepEqual(
    parseCgroupEvents("populated 0\n"),
    { populated: false },
  );
  for (const malformed of [
    "",
    "populated 2\n",
    "populated 0",
    "populated 0\npopulated 1\n",
    "populated nope\n",
  ]) {
    assert.throws(() => parseCgroupEvents(malformed), /events|populated/);
  }
});

test("releases only exact attested systemd scope leader", async () => {
  const requested = [];
  let identityReads = 0;
  const released = await releaseAttestedScopeBarrier(evidence(), {
    systemctl: "/usr/bin/systemctl",
    read: async (path) => {
      if (path.endsWith("/stat")) {
        identityReads += 1;
        return stoppedStat(1234, 5678);
      }
      if (path.endsWith("/status")) {
        return Buffer.from("State:\tT (stopped)\n");
      }
      if (path.endsWith("/cgroup")) {
        return Buffer.from(`0::${controlGroup}\n`);
      }
      if (path.endsWith("/cgroup.procs")) {
        return Buffer.from("1234\n");
      }
      throw new Error(`unexpected process identity path ${path}`);
    },
    canonical: async (path) => path,
    metadata: async () => ({ isFile: () => true }),
    controlGroupFor: async (_systemctl, requestedUnit) => {
      requested.push(["control-group", requestedUnit]);
      return requestedUnit === unit ? controlGroup : sliceControlGroup;
    },
    signalScope: async (...args) => {
      requested.push(["signal", ...args]);
    },
  });
  assert.equal(released, unit);
  assert.equal(identityReads, 2);
  assert.deepEqual(requested.at(-1), [
    "signal",
    "/usr/bin/systemctl",
    unit,
    slice,
  ]);
  assert.deepEqual(scopeBarrierSignalArguments(unit, slice), [
    "--user",
    "kill",
    "--kill-whom=all",
    "--signal=CONT",
    "--",
    unit,
  ]);
});

test("rejects PID reuse before unit-addressed barrier release", async () => {
  let signals = 0;
  await assert.rejects(
    releaseAttestedScopeBarrier(evidence(), {
      systemctl: "/usr/bin/systemctl",
      read: async (path) => {
        if (path.endsWith("/stat")) return stoppedStat(1234, 9999);
        if (path.endsWith("/status")) {
          return Buffer.from("State:\tT (stopped)\n");
        }
        return Buffer.from(`0::${controlGroup}\n`);
      },
      controlGroupFor: async (_systemctl, requestedUnit) =>
        requestedUnit === unit ? controlGroup : sliceControlGroup,
      signalScope: async () => {
        signals += 1;
      },
    }),
    /scope leader changed/,
  );
  assert.equal(signals, 0);
});

test("rejects extra cgroup member before unit barrier release", async () => {
  let signals = 0;
  await assert.rejects(
    releaseAttestedScopeBarrier(evidence(), {
      systemctl: "/usr/bin/systemctl",
      read: async (path) => {
        if (path.endsWith("/stat")) return stoppedStat(1234, 5678);
        if (path.endsWith("/status")) {
          return Buffer.from("State:\tT (stopped)\n");
        }
        if (path.endsWith("/cgroup")) {
          return Buffer.from(`0::${controlGroup}\n`);
        }
        return Buffer.from("1234\n4321\n");
      },
      canonical: async (path) => path,
      metadata: async () => ({ isFile: () => true }),
      controlGroupFor: async (_systemctl, requestedUnit) =>
        requestedUnit === unit ? controlGroup : sliceControlGroup,
      signalScope: async () => {
        signals += 1;
      },
    }),
    /does not contain only attested stopped leader/,
  );
  assert.equal(signals, 0);
});

test("rejects extra attested evidence before barrier release", async () => {
  const candidate = { ...evidence(), unexpected: true };
  await assert.rejects(
    releaseAttestedScopeBarrier(candidate, {
      systemctl: "/usr/bin/systemctl",
    }),
    /attested process scope evidence is invalid/,
  );
});

test("writes exactly one cgroup.kill request and waits for empty", async () => {
  const reads = new Map([
    [
      evidence().eventsPath,
      [
        Buffer.from("populated 1\n"),
        Buffer.from("populated 1\n"),
        Buffer.from("populated 0\n"),
      ],
    ],
    [evidence().sliceEventsPath, [Buffer.from("populated 0\n")]],
  ]);
  const writes = [];
  const cleaned = await cleanupAttestedScope(evidence(), {
    commandExitStatus: 0,
    ...retiredSliceOptions(),
    read: async (path) => reads.get(path).shift(),
    write: async (...args) => writes.push(args),
    wait: async () => {},
    validateUnitControls: async () => ({
      cgroupPath: evidence().cgroupPath,
      eventsPath: evidence().eventsPath,
      killPath: evidence().killPath,
    }),
    validateSliceControls: async () => ({
      cgroupPath: evidence().sliceCgroupPath,
      eventsPath: evidence().sliceEventsPath,
      killPath: evidence().sliceKillPath,
    }),
  });
  assert.equal(cleaned.status, "cleaned");
  assert.deepEqual(cleaned.cleanup, {
    status: "passed",
    initiallyPopulated: true,
    residueKilled: true,
    finalPopulated: false,
    sliceInitiallyPopulated: false,
    sliceResidueKilled: false,
    sliceFinalPopulated: false,
  });
  assert.deepEqual(writes, [[evidence().killPath, "1\n", { flag: "w" }]]);
});

test("does not signal an already empty attested scope", async () => {
  let writes = 0;
  const cleaned = await cleanupAttestedScope(evidence(), {
    commandExitStatus: 0,
    ...retiredSliceOptions(),
    read: async () => Buffer.from("populated 0\n"),
    write: async () => {
      writes += 1;
    },
    validateUnitControls: async () => ({
      cgroupPath: evidence().cgroupPath,
      eventsPath: evidence().eventsPath,
      killPath: evidence().killPath,
    }),
    validateSliceControls: async () => ({
      cgroupPath: evidence().sliceCgroupPath,
      eventsPath: evidence().sliceEventsPath,
      killPath: evidence().sliceKillPath,
    }),
  });
  assert.equal(writes, 0);
  assert.equal(cleaned.cleanup.residueKilled, false);
});

test("unknown interrupted scope status is durably tainted", async () => {
  const cleaned = await cleanupAttestedScope(evidence(), {
    ...retiredSliceOptions(),
    read: async () => Buffer.from("populated 0\n"),
    validateUnitControls: async () => ({
      cgroupPath: evidence().cgroupPath,
      eventsPath: evidence().eventsPath,
      killPath: evidence().killPath,
    }),
    validateSliceControls: async () => ({
      cgroupPath: evidence().sliceCgroupPath,
      eventsPath: evidence().sliceEventsPath,
      killPath: evidence().sliceKillPath,
    }),
  });
  assert.equal(cleaned.commandExitStatus, 255);
  assert.throws(
    () => validateCleanedScopeEvidence(cleaned, { gate: "gate2", slice }),
    /cleaned process scope evidence is invalid/,
  );
  const reopened = await cleanupAttestedScope(cleaned, {
    ...retiredSliceOptions(),
    read: async () => Buffer.from("populated 0\n"),
    validateUnitControls: async () => undefined,
    validateSliceControls: async () => undefined,
  });
  assert.equal(reopened.commandExitStatus, 255);
});

test("kills residue that escaped the gate unit but stayed in run slice", async () => {
  const reads = new Map([
    [evidence().eventsPath, [Buffer.from("populated 0\n")]],
    [
      evidence().sliceEventsPath,
      [
        Buffer.from("populated 1\n"),
        Buffer.from("populated 0\n"),
      ],
    ],
  ]);
  const writes = [];
  const cleaned = await cleanupAttestedScope(evidence(), {
    commandExitStatus: 0,
    ...retiredSliceOptions(),
    read: async (path) => reads.get(path).shift(),
    write: async (...args) => writes.push(args),
    wait: async () => {},
    validateUnitControls: async () => ({
      cgroupPath: evidence().cgroupPath,
      eventsPath: evidence().eventsPath,
      killPath: evidence().killPath,
    }),
    validateSliceControls: async () => ({
      cgroupPath: evidence().sliceCgroupPath,
      eventsPath: evidence().sliceEventsPath,
      killPath: evidence().sliceKillPath,
    }),
  });
  assert.equal(cleaned.cleanup.residueKilled, false);
  assert.equal(cleaned.cleanup.sliceResidueKilled, true);
  assert.deepEqual(
    writes,
    [[evidence().sliceKillPath, "1\n", { flag: "w" }]],
  );
});

test("planned recovery kills slice residue after gate unit disappeared", async () => {
  const reads = [
    Buffer.from("populated 1\n"),
    Buffer.from("populated 0\n"),
  ];
  const writes = [];
  let stopped = false;
  const recovered = await cleanupPlannedScope({
    unit,
    slice,
    systemctl: "/usr/bin/systemctl",
    cgroupRoot: "/test-cgroup",
    controlGroupFor: async (_systemctl, requested) => {
      if (requested === unit) return controlGroup;
      if (requested === slice && !stopped) return sliceControlGroup;
      return undefined;
    },
    read: async () => reads.shift(),
    write: async (...args) => writes.push(args),
    wait: async () => {},
    validateMount: async () => {},
    validateControls: async (_root, requested) => {
      if (requested === controlGroup) {
        throw Object.assign(new Error("scope disappeared"), {
          code: "ENOENT",
        });
      }
      return {
        cgroupPath: `/test-cgroup${sliceControlGroup}`,
        eventsPath: `/test-cgroup${sliceControlGroup}/cgroup.events`,
        killPath: `/test-cgroup${sliceControlGroup}/cgroup.kill`,
      };
    },
    stopSlice: async (_systemctl, requested) => {
      assert.equal(requested, slice);
      stopped = true;
    },
    activeStateFor: async () => "inactive",
    loadedStateFor: async () => false,
  });
  assert.deepEqual(recovered, { residueKilled: true });
  assert.deepEqual(writes, [[
    `/test-cgroup${sliceControlGroup}/cgroup.kill`,
    "1\n",
    { flag: "w" },
  ]]);
  assert.equal(stopped, true);
});

test("retires exact empty slice and verifies manager unload", async () => {
  const operations = [];
  let stopped = false;
  const retired = await retireScopeSlice({
    slice,
    systemctl: "/usr/bin/systemctl",
    cgroupRoot: "/test-cgroup",
    expectedControlGroup: sliceControlGroup,
    controlGroupFor: async (_systemctl, requested) => {
      assert.equal(requested, slice);
      return stopped ? undefined : sliceControlGroup;
    },
    read: async (path) => {
      operations.push(["read", path]);
      return Buffer.from("populated 0\n");
    },
    validateMount: async () => {},
    validateControls: async () => ({
      cgroupPath: `/test-cgroup${sliceControlGroup}`,
      eventsPath: `/test-cgroup${sliceControlGroup}/cgroup.events`,
      killPath: `/test-cgroup${sliceControlGroup}/cgroup.kill`,
    }),
    stopSlice: async (_systemctl, requested) => {
      operations.push(["stop", requested]);
      stopped = true;
    },
    activeStateFor: async () => "inactive",
    loadedStateFor: async () => false,
  });
  assert.deepEqual(retired, { residueKilled: false });
  assert.deepEqual(operations, [
    [
      "read",
      `/test-cgroup${sliceControlGroup}/cgroup.events`,
    ],
    ["stop", slice],
  ]);
});

test("already absent slice needs no stop request", async () => {
  let stops = 0;
  const retired = await retireScopeSlice({
    slice,
    systemctl: "/usr/bin/systemctl",
    cgroupRoot: "/test-cgroup",
    controlGroupFor: async () => undefined,
    validateMount: async () => {},
    stopSlice: async () => {
      stops += 1;
    },
    activeStateFor: async () => "inactive",
    loadedStateFor: async () => false,
  });
  assert.deepEqual(retired, { residueKilled: false });
  assert.equal(stops, 0);
});

test("refuses altered attestation paths before signaling", async () => {
  const altered = { ...evidence(), killPath: "/sys/fs/cgroup/other/kill" };
  let writes = 0;
  await assert.rejects(
    cleanupAttestedScope(altered, {
      ...retiredSliceOptions(),
      write: async () => {
        writes += 1;
      },
      validateUnitControls: async () => {
        throw new Error("must not validate");
      },
      validateSliceControls: async () => {
        throw new Error("must not validate");
      },
    }),
    /evidence paths differ/,
  );
  assert.equal(writes, 0);
});
