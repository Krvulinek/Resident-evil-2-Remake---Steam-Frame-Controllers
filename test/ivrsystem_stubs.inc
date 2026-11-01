    void GetRecommendedRenderTargetSize( uint32_t *pnWidth, uint32_t *pnHeight ) override {  }
    HmdMatrix44_t GetProjectionMatrix( EVREye eEye, float fNearZ, float fFarZ ) override { return {}; }
    void GetProjectionRaw( EVREye eEye, float *pfLeft, float *pfRight, float *pfTop, float *pfBottom ) override {  }
    bool ComputeDistortion( EVREye eEye, float fU, float fV, DistortionCoordinates_t *pDistortionCoordinates ) override { return {}; }
    HmdMatrix34_t GetEyeToHeadTransform( EVREye eEye ) override { return {}; }
    bool GetTimeSinceLastVsync( float *pfSecondsSinceLastVsync, uint64_t *pulFrameCounter ) override { return {}; }
    int32_t GetD3D9AdapterIndex() override { return {}; }
    void GetDXGIOutputInfo( int32_t *pnAdapterIndex ) override {  }
    void GetOutputDevice( uint64_t *pnDevice, ETextureType textureType, VkInstance_T *pInstance = nullptr ) override {  }
    bool IsDisplayOnDesktop() override { return {}; }
    bool SetDisplayVisibility( bool bIsVisibleOnDesktop ) override { return {}; }
    void GetDeviceToAbsoluteTrackingPose( ETrackingUniverseOrigin eOrigin, float fPredictedSecondsToPhotonsFromNow,  TrackedDevicePose_t *pTrackedDevicePoseArray, uint32_t unTrackedDevicePoseArrayCount ) override;
    HmdMatrix34_t GetSeatedZeroPoseToStandingAbsoluteTrackingPose() override { return {}; }
    HmdMatrix34_t GetRawZeroPoseToStandingAbsoluteTrackingPose() override { return {}; }
    uint32_t GetSortedTrackedDeviceIndicesOfClass( ETrackedDeviceClass eTrackedDeviceClass,  vr::TrackedDeviceIndex_t *punTrackedDeviceIndexArray, uint32_t unTrackedDeviceIndexArrayCount, vr::TrackedDeviceIndex_t unRelativeToTrackedDeviceIndex = k_unTrackedDeviceIndex_Hmd ) override { return {}; }
    EDeviceActivityLevel GetTrackedDeviceActivityLevel( vr::TrackedDeviceIndex_t unDeviceId ) override { return {}; }
    void ApplyTransform( TrackedDevicePose_t *pOutputPose, const TrackedDevicePose_t *pTrackedDevicePose, const HmdMatrix34_t *pTransform ) override {  }
    vr::TrackedDeviceIndex_t GetTrackedDeviceIndexForControllerRole( vr::ETrackedControllerRole unDeviceType ) override;
    vr::ETrackedControllerRole GetControllerRoleForTrackedDeviceIndex( vr::TrackedDeviceIndex_t unDeviceIndex ) override { return {}; }
    ETrackedDeviceClass GetTrackedDeviceClass( vr::TrackedDeviceIndex_t unDeviceIndex ) override { return {}; }
    bool IsTrackedDeviceConnected( vr::TrackedDeviceIndex_t unDeviceIndex ) override { return {}; }
    bool GetBoolTrackedDeviceProperty( vr::TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, ETrackedPropertyError *pError = 0L ) override { return {}; }
    float GetFloatTrackedDeviceProperty( vr::TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, ETrackedPropertyError *pError = 0L ) override { return {}; }
    int32_t GetInt32TrackedDeviceProperty( vr::TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, ETrackedPropertyError *pError = 0L ) override { return {}; }
    uint64_t GetUint64TrackedDeviceProperty( vr::TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, ETrackedPropertyError *pError = 0L ) override { return {}; }
    HmdMatrix34_t GetMatrix34TrackedDeviceProperty( vr::TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, ETrackedPropertyError *pError = 0L ) override { return {}; }
    uint32_t GetArrayTrackedDeviceProperty( vr::TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, PropertyTypeTag_t propType, void *pBuffer, uint32_t unBufferSize, ETrackedPropertyError *pError = 0L ) override { return {}; }
    uint32_t GetStringTrackedDeviceProperty( vr::TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop,  char *pchValue, uint32_t unBufferSize, ETrackedPropertyError *pError = 0L ) override;
    const char *GetPropErrorNameFromEnum( ETrackedPropertyError error ) override { return nullptr; }
    bool PollNextEvent( VREvent_t *pEvent, uint32_t uncbVREvent ) override { return {}; }
    bool PollNextEventWithPose( ETrackingUniverseOrigin eOrigin, VREvent_t *pEvent, uint32_t uncbVREvent, vr::TrackedDevicePose_t *pTrackedDevicePose ) override { return {}; }
    const char *GetEventTypeNameFromEnum( EVREventType eType ) override { return nullptr; }
    HiddenAreaMesh_t GetHiddenAreaMesh( EVREye eEye, EHiddenAreaMeshType type = k_eHiddenAreaMesh_Standard ) override { return {}; }
    bool GetControllerState( vr::TrackedDeviceIndex_t unControllerDeviceIndex, vr::VRControllerState_t *pControllerState, uint32_t unControllerStateSize ) override { return {}; }
    bool GetControllerStateWithPose( ETrackingUniverseOrigin eOrigin, vr::TrackedDeviceIndex_t unControllerDeviceIndex, vr::VRControllerState_t *pControllerState, uint32_t unControllerStateSize, TrackedDevicePose_t *pTrackedDevicePose ) override { return {}; }
    void TriggerHapticPulse( vr::TrackedDeviceIndex_t unControllerDeviceIndex, uint32_t unAxisId, unsigned short usDurationMicroSec ) override {  }
    const char *GetButtonIdNameFromEnum( EVRButtonId eButtonId ) override { return nullptr; }
    const char *GetControllerAxisTypeNameFromEnum( EVRControllerAxisType eAxisType ) override { return nullptr; }
    bool IsInputAvailable() override { return {}; }
    bool IsSteamVRDrawingControllers() override { return {}; }
    bool ShouldApplicationPause() override { return {}; }
    bool ShouldApplicationReduceRenderingWork() override { return {}; }
    vr::EVRFirmwareError PerformFirmwareUpdate( vr::TrackedDeviceIndex_t unDeviceIndex ) override { return {}; }
    void AcknowledgeQuit_Exiting() override {  }
    uint32_t GetAppContainerFilePaths(  char *pchBuffer, uint32_t unBufferSize ) override { return {}; }
    const char *GetRuntimeVersion() override { return nullptr; }
