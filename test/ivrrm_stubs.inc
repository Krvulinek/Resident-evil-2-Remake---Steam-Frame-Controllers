    EVRRenderModelError LoadRenderModel_Async( const char *pchRenderModelName, RenderModel_t **ppRenderModel ) override { return {}; }
    void FreeRenderModel( RenderModel_t *pRenderModel ) override {  }
    EVRRenderModelError LoadTexture_Async( TextureID_t textureId, RenderModel_TextureMap_t **ppTexture ) override { return {}; }
    void FreeTexture( RenderModel_TextureMap_t *pTexture ) override {  }
    EVRRenderModelError LoadTextureD3D11_Async( TextureID_t textureId, void *pD3D11Device, void **ppD3D11Texture2D ) override { return {}; }
    EVRRenderModelError LoadIntoTextureD3D11_Async( TextureID_t textureId, void *pDstTexture ) override { return {}; }
    void FreeTextureD3D11( void *pD3D11Texture2D ) override {  }
    uint32_t GetRenderModelName( uint32_t unRenderModelIndex,  char *pchRenderModelName, uint32_t unRenderModelNameLen ) override { return {}; }
    uint32_t GetRenderModelCount() override { return {}; }
    uint32_t GetComponentCount( const char *pchRenderModelName ) override { return {}; }
    uint32_t GetComponentName( const char *pchRenderModelName, uint32_t unComponentIndex,  char *pchComponentName, uint32_t unComponentNameLen ) override { return {}; }
    uint64_t GetComponentButtonMask( const char *pchRenderModelName, const char *pchComponentName ) override { return {}; }
    uint32_t GetComponentRenderModelName( const char *pchRenderModelName, const char *pchComponentName,  char *pchComponentRenderModelName, uint32_t unComponentRenderModelNameLen ) override { return {}; }
    bool GetComponentStateForDevicePath( const char *pchRenderModelName, const char *pchComponentName, vr::VRInputValueHandle_t devicePath, const vr::RenderModel_ControllerMode_State_t *pState, vr::RenderModel_ComponentState_t *pComponentState ) override;
    bool GetComponentState( const char *pchRenderModelName, const char *pchComponentName, const vr::VRControllerState_t *pControllerState, const RenderModel_ControllerMode_State_t *pState, RenderModel_ComponentState_t *pComponentState ) override { return {}; }
    bool RenderModelHasComponent( const char *pchRenderModelName, const char *pchComponentName ) override { return {}; }
    uint32_t GetRenderModelThumbnailURL( const char *pchRenderModelName,  char *pchThumbnailURL, uint32_t unThumbnailURLLen, vr::EVRRenderModelError *peError ) override { return {}; }
    uint32_t GetRenderModelOriginalPath( const char *pchRenderModelName,  char *pchOriginalPath, uint32_t unOriginalPathLen, vr::EVRRenderModelError *peError ) override { return {}; }
    const char *GetRenderModelErrorNameFromEnum( vr::EVRRenderModelError error ) override { return nullptr; }
