    EVRInputError SetActionManifestPath( const char *pchActionManifestPath ) override;
    EVRInputError GetActionSetHandle( const char *pchActionSetName, VRActionSetHandle_t *pHandle ) override;
    EVRInputError GetActionHandle( const char *pchActionName, VRActionHandle_t *pHandle ) override;
    EVRInputError GetInputSourceHandle( const char *pchInputSourcePath, VRInputValueHandle_t *pHandle ) override;
    EVRInputError UpdateActionState(  VRActiveActionSet_t *pSets, uint32_t unSizeOfVRSelectedActionSet_t, uint32_t unSetCount ) override;
    EVRInputError GetDigitalActionData( VRActionHandle_t action, InputDigitalActionData_t *pActionData, uint32_t unActionDataSize, VRInputValueHandle_t ulRestrictToDevice ) override;
    EVRInputError GetAnalogActionData( VRActionHandle_t action, InputAnalogActionData_t *pActionData, uint32_t unActionDataSize, VRInputValueHandle_t ulRestrictToDevice ) override;
    EVRInputError GetPoseActionDataRelativeToNow( VRActionHandle_t action, ETrackingUniverseOrigin eOrigin, float fPredictedSecondsFromNow, InputPoseActionData_t *pActionData, uint32_t unActionDataSize, VRInputValueHandle_t ulRestrictToDevice ) override { return {}; }
    EVRInputError GetPoseActionDataForNextFrame( VRActionHandle_t action, ETrackingUniverseOrigin eOrigin, InputPoseActionData_t *pActionData, uint32_t unActionDataSize, VRInputValueHandle_t ulRestrictToDevice ) override { return {}; }
    EVRInputError GetSkeletalActionData( VRActionHandle_t action, InputSkeletalActionData_t *pActionData, uint32_t unActionDataSize ) override { return {}; }
    EVRInputError GetDominantHand( ETrackedControllerRole *peDominantHand ) override { return {}; }
    EVRInputError SetDominantHand( ETrackedControllerRole eDominantHand ) override { return {}; }
    EVRInputError GetBoneCount( VRActionHandle_t action, uint32_t* pBoneCount ) override { return {}; }
    EVRInputError GetBoneHierarchy( VRActionHandle_t action,  BoneIndex_t* pParentIndices, uint32_t unIndexArayCount ) override { return {}; }
    EVRInputError GetBoneName( VRActionHandle_t action, BoneIndex_t nBoneIndex,  char* pchBoneName, uint32_t unNameBufferSize ) override { return {}; }
    EVRInputError GetSkeletalReferenceTransforms( VRActionHandle_t action, EVRSkeletalTransformSpace eTransformSpace, EVRSkeletalReferencePose eReferencePose,  VRBoneTransform_t *pTransformArray, uint32_t unTransformArrayCount ) override { return {}; }
    EVRInputError GetSkeletalTrackingLevel( VRActionHandle_t action, EVRSkeletalTrackingLevel* pSkeletalTrackingLevel ) override { return {}; }
    EVRInputError GetSkeletalBoneData( VRActionHandle_t action, EVRSkeletalTransformSpace eTransformSpace, EVRSkeletalMotionRange eMotionRange,  VRBoneTransform_t *pTransformArray, uint32_t unTransformArrayCount ) override { return {}; }
    EVRInputError GetSkeletalSummaryData( VRActionHandle_t action, EVRSummaryType eSummaryType, VRSkeletalSummaryData_t * pSkeletalSummaryData ) override { return {}; }
    EVRInputError GetSkeletalBoneDataCompressed( VRActionHandle_t action, EVRSkeletalMotionRange eMotionRange,  void *pvCompressedData, uint32_t unCompressedSize, uint32_t *punRequiredCompressedSize ) override { return {}; }
    EVRInputError DecompressSkeletalBoneData( const void *pvCompressedBuffer, uint32_t unCompressedBufferSize, EVRSkeletalTransformSpace eTransformSpace,  VRBoneTransform_t *pTransformArray, uint32_t unTransformArrayCount ) override { return {}; }
    EVRInputError TriggerHapticVibrationAction( VRActionHandle_t action, float fStartSecondsFromNow, float fDurationSeconds, float fFrequency, float fAmplitude, VRInputValueHandle_t ulRestrictToDevice ) override { return {}; }
    EVRInputError GetActionOrigins( VRActionSetHandle_t actionSetHandle, VRActionHandle_t digitalActionHandle,  VRInputValueHandle_t *originsOut, uint32_t originOutCount ) override { return {}; }
    EVRInputError GetOriginLocalizedName( VRInputValueHandle_t origin,  char *pchNameArray, uint32_t unNameArraySize, int32_t unStringSectionsToInclude ) override;
    EVRInputError GetOriginTrackedDeviceInfo( VRInputValueHandle_t origin, InputOriginInfo_t *pOriginInfo, uint32_t unOriginInfoSize ) override { return {}; }
    EVRInputError GetActionBindingInfo( VRActionHandle_t action, InputBindingInfo_t *pOriginInfo, uint32_t unBindingInfoSize, uint32_t unBindingInfoCount, uint32_t *punReturnedBindingInfoCount ) override { return {}; }
    EVRInputError ShowActionOrigins( VRActionSetHandle_t actionSetHandle, VRActionHandle_t ulActionHandle ) override { return {}; }
    EVRInputError ShowBindingsForActionSet(  VRActiveActionSet_t *pSets, uint32_t unSizeOfVRSelectedActionSet_t, uint32_t unSetCount, VRInputValueHandle_t originToHighlight ) override { return {}; }
    EVRInputError GetComponentStateForBinding( const char *pchRenderModelName, const char *pchComponentName, const InputBindingInfo_t *pOriginInfo, uint32_t unBindingInfoSize, uint32_t unBindingInfoCount, vr::RenderModel_ComponentState_t *pComponentState ) override { return {}; }
    bool IsUsingLegacyInput() override { return {}; }
    EVRInputError OpenBindingUI( const char* pchAppKey, VRActionSetHandle_t ulActionSetHandle, VRInputValueHandle_t ulDeviceHandle, bool bShowOnDesktop ) override { return {}; }
    EVRInputError GetBindingVariant( vr::VRInputValueHandle_t ulDevicePath,  char *pchVariantArray, uint32_t unVariantArraySize ) override { return {}; }
