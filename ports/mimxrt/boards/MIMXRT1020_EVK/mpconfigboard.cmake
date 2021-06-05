set(MCU_SERIES MIMXRT1021)
set(MCU_VARIANT MIMXRT1021DAG5A)


set(MICROPY_FLOAT_IMPL double)
set(SUPPORTS_HARDWARE_FP_SINGLE 0)
set(SUPPORTS_HARDWARE_FP_DOUBLE 0)



if(NOT JLINK_PATH)
	set(JLINK_PATH /media/RT1020-EVK/)
endif()
set(JLINK_COMMANDER_SCRIPT ${CMAKE_BINARY_DIR}/script.jlink)

if(JLINK_IP)
	set(JLINK_CONNECTION_SETTINGS "-IP ${LINK_IP}")	
else()
	set(JLINK_CONNECTION_SETTINGS)
endif()


add_custom_target(deploy_jlink ALL
	DEPENDS ${MICROPY_TARGET}
	COMMAND echo "ExitOnError 1" > ${JLINK_COMMANDER_SCRIPT}
	COMMAND echo "speed auto" >> ${JLINK_COMMANDER_SCRIPT}
	COMMAND echo "r" >> ${JLINK_COMMANDER_SCRIPT}
	COMMAND echo "st" >> ${JLINK_COMMANDER_SCRIPT}
	COMMAND echo "loadfile \"${CMAKE_BINARY_DIR}/${MICROPY_TARGET}.hex\"" >> ${JLINK_COMMANDER_SCRIPT}
	COMMAND echo "qc" >> ${JLINK_COMMANDER_SCRIPT}
	COMMAND ${JLINK_PATH}JLinkExe -device ${MCU_VARIANT} -if SWD ${JLINK_CONNECTION_SETTINGS} -CommanderScript ${JLINK_COMMANDER_SCRIPT}
)

add_custom_target(deploy ALL
	DEPENDS ${MICROPY_TARGET}
	COMMAND cp ${TARGET}.bin ${JLINK_PATH}
)
