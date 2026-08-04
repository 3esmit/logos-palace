function validDialogId(dialog) {
  const objectId = String(dialog?.id ?? "");
  if (objectId.length === 0 || objectId.length > 512) {
    throw new Error("asset picker dialog identity is invalid");
  }
  return objectId;
}

export function uniqueFileDialogIds(dialogs) {
  if (!Array.isArray(dialogs)) return undefined;
  return new Set(dialogs.map(validDialogId));
}

export function newlyOpenedFileDialogId(dialogIds, priorDialogIds) {
  if (!(dialogIds instanceof Set) || !(priorDialogIds instanceof Set)) {
    throw new Error("asset picker dialog set is invalid");
  }
  const freshIds = [...dialogIds].filter(
    (objectId) => !priorDialogIds.has(objectId),
  );
  if (freshIds.length === 0) return undefined;
  if (freshIds.length > 1) {
    throw new Error("asset picker opened multiple dialogs");
  }
  return freshIds[0];
}
