---
description: How to run terminal commands on this Windows system
---

# Running Commands Workaround

On this Windows system, the `run_command` tool does not execute commands directly. The command prompt opens but the command doesn't run.

## Workaround Steps

1. First, open a cmd terminal using `run_command` with just `cmd`:

```
run_command:
  CommandLine: cmd
  Cwd: <project_directory>
  WaitMsBeforeAsync: 1000
```

2. Then, send the actual command using `send_command_input`:

```
send_command_input:
  CommandId: <id_from_step_1>
  Input: <your_command>\n
  WaitMs: 10000
```

## Example: Running PlatformIO Build

// turbo

1. Open cmd terminal:

```
run_command:
  CommandLine: cmd
  Cwd: c:\Users\Chanakun\Documents\PlatformIO\Projects\test
  WaitMsBeforeAsync: 1000
```

// turbo 2. Send the pio run command:

```
send_command_input:
  CommandId: <from_previous_step>
  Input: pio run\n
  WaitMs: 15000
```

## Important Notes

- Always use this two-step approach for running commands
- Include `\n` (newline) at the end of the Input to simulate pressing Enter
- Adjust WaitMs based on expected command duration
