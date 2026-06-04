# T-Embed BLE Recovery Checkpoint

The full BLE/T-Embed diagnostic and recovery checkpoint is preserved at:

```bash
backup/tembed-ble-recovery-checkpoint
```

To reapply that checkpoint onto the current branch:

```bash
git cherry-pick backup/tembed-ble-recovery-checkpoint
```

Exact checkpoint commit:

```bash
e8f92ad Checkpoint T-Embed BLE recovery path
```
