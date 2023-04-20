include("$(PORT_DIR)/boards/manifest.py")
include("$(MPY_DIR)/extmod/asyncio")
require("onewire")
require("ds18x20")
require("dht")

include(
    "$(MPY_LIB_DIR)/micropython/bluetooth/aioble/manifest.py",
    client=True,
    central=True,
    l2cap=True,
    security=True,
)
