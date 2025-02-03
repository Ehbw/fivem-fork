---
ns: CFX
apiset: client
game: gta5
---
## SET_VEHICLE_DOOR_ANGLE_RATIO

```c
void SET_VEHICLE_DOOR_ANGLE_RATIO(Vehicle vehicle, int doorIndex, float angleRatio);
```

Setter for [GET_VEHICLE_DOOR_ANGLE_RATIO](?_0xFE3F9C29F7B32BD5)

## Parameters
* **vehicle**: The vehicles handle
* **doorIndex**: Index of the vehicles door. See eDoorId in [SET_VEHICLE_DOOR_SHUT](?_0x93D9BD300D7789E5)
* **angleRatio**: The new angle for the door. Between 0.0 and 1.0
