// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Types.h"
#include "../../../Core/Exceptions.h"

namespace RenderCore { namespace Metal_AppleMetal
{
    class DeviceContext;

    using StencilDesc = RenderCore::StencilDesc;
	using DepthStencilDesc = RenderCore::DepthStencilDesc;
	using RasterizationDesc = RenderCore::RasterizationDesc;
	using SamplerStateDesc = RenderCore::SamplerStateDesc;
	using AttachmentBlendDesc = RenderCore::AttachmentBlendDesc;

////////////////////////////////////////////////////////////////////////////////////////////////////

    class SamplerState
    {
    public:
        SamplerState(   FilterMode filter,
                        AddressMode addressU = AddressMode::Wrap,
                        AddressMode addressV = AddressMode::Wrap,
                        AddressMode addressW = AddressMode::Wrap,
                        CompareOp comparison = CompareOp::Never,
                        bool enableMipmaps = true);
        SamplerState(); // inexpensive default constructor

        void Apply(DeviceContext& context, bool textureHasMipmaps, unsigned samplerIndex, ShaderStage stage) const never_throws;

        typedef SamplerState UnderlyingType;
        UnderlyingType GetUnderlying() const never_throws { return *this; }

    private:
        class Pimpl;
        std::shared_ptr<Pimpl> _pimpl;
    };

    class BlendState
    {
    public:
        BlendState();
        void Apply() const never_throws;
    };

////////////////////////////////////////////////////////////////////////////////////////////////////

}}

