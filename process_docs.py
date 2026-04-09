import os
import re

files = {
    'docs/pcb/PCB_OVERVIEW.md': {
        'breadcrumb': '[Repository Root](../../README.md) > PCB Overview',
        'related': '## Related Documents\n\n- [Communications](COMS/COMS.md)\n- [Microcontroller Unit](MCU/MCU.md)\n- [Power Management](POWER/POWER.md)\n- [Sensors](SENSORS/SENSORS.md)\n'
    },
    'docs/pcb/POWER/POWER.md': {
        'breadcrumb': '[Repository Root](../../../README.md) > [PCB Overview](../../PCB_OVERVIEW.md) > Power Management',
        'related': '## Related Documents\n\n- [PCB Overview](../../PCB_OVERVIEW.md)\n- [Communications](../COMS/COMS.md)\n- [Microcontroller Unit](../MCU/MCU.md)\n- [Sensors](../SENSORS/SENSORS.md)\n'
    },
    'docs/pcb/MCU/MCU.md': {
        'breadcrumb': '[Repository Root](../../../README.md) > [PCB Overview](../../PCB_OVERVIEW.md) > Microcontroller Unit',
        'related': '## Related Documents\n\n- [PCB Overview](../../PCB_OVERVIEW.md)\n- [Communications](../COMS/COMS.md)\n- [Power Management](../POWER/POWER.md)\n- [Sensors](../SENSORS/SENSORS.md)\n'
    },
    'docs/pcb/COMS/COMS.md': {
        'breadcrumb': '[Repository Root](../../../README.md) > [PCB Overview](../../PCB_OVERVIEW.md) > Communications',
        'related': '## Related Documents\n\n- [PCB Overview](../../PCB_OVERVIEW.md)\n- [Microcontroller Unit](../MCU/MCU.md)\n- [Power Management](../POWER/POWER.md)\n- [Sensors](../SENSORS/SENSORS.md)\n'
    },
    'docs/pcb/SENSORS/SENSORS.md': {
        'breadcrumb': '[Repository Root](../../../README.md) > [PCB Overview](../../PCB_OVERVIEW.md) > Sensors',
        'related': '## Related Documents\n\n- [PCB Overview](../../PCB_OVERVIEW.md)\n- [Communications](../COMS/COMS.md)\n- [Microcontroller Unit](../MCU/MCU.md)\n- [Power Management](../POWER/POWER.md)\n'
    }
}

for path, info in files.items():
    with open(path, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # Remove schematic placeholder completely including section headers
    content = re.sub(r'## Schematic\n\n>\s*_Insert schematic screenshot here:\s*`[^`]+`_\n\n---\n\n', '', content)
    # Remove TBD placeholders
    content = re.sub(r'TBD \([^)]+\)', '', content)
    content = re.sub(r'TBD ', '', content)
    content = re.sub(r'TBD', '', content)
    
    # Check if breadcrumb already exists to avoid duplication
    if not content.startswith('['):
        new_content = info['breadcrumb'] + '\n\n' + content + '\n\n---\n\n' + info['related']
        with open(path, 'w', encoding='utf-8') as f:
            f.write(new_content)
