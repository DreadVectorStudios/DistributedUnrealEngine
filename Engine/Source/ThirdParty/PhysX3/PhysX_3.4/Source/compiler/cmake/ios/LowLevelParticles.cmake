#
# Build LowLevelParticles
#

SET(PHYSX_SOURCE_DIR ${PROJECT_SOURCE_DIR}/../../../)

SET(LL_SOURCE_DIR ${PHYSX_SOURCE_DIR}/LowLevelParticles/src)

FIND_PACKAGE(nvToolsExt REQUIRED)

SET(LOWLEVELPARTICLES_PLATFORM_INCLUDES
	${NVTOOLSEXT_INCLUDE_DIRS}
)

SET(LOWLEVELPARTICLES_PLATFORM_SRC_FILES
	${LL_SOURCE_DIR}/gpu/PtRigidBodyAccessGpu.cpp
)

# Use generator expressions to set config specific preprocessor definitions
SET(LOWLEVELPARTICLES_COMPILE_DEFS 

	${PHYSX_IOS_COMPILE_DEFS};PX_PHYSX_STATIC_LIB

	$<$<CONFIG:debug>:${PHYSX_IOS_DEBUG_COMPILE_DEFS};PX_PHYSX_DLL_NAME_POSTFIX=DEBUG;>
	$<$<CONFIG:checked>:${PHYSX_IOS_CHECKED_COMPILE_DEFS};PX_PHYSX_DLL_NAME_POSTFIX=CHECKED;>
	$<$<CONFIG:profile>:${PHYSX_IOS_PROFILE_COMPILE_DEFS};PX_PHYSX_DLL_NAME_POSTFIX=PROFILE;>
	$<$<CONFIG:release>:${PHYSX_IOS_RELEASE_COMPILE_DEFS};>
)

# include common low level particles settings
INCLUDE(../common/LowLevelParticles.cmake)

# enable -fPIC so we can link static libs with the editor
SET_TARGET_PROPERTIES(LowLevelParticles PROPERTIES POSITION_INDEPENDENT_CODE TRUE)