#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_COMMANDRECORDER_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_COMMANDRECORDER_H_
#include "graphics/host_gpu/renderer/commandScheduler.h"
#include <array>

namespace Libs::Graphics {
// Senaxx 5fa52d41 / IDXTRI recorder interface. All pointed-to arrays are deep-copied
// before returning; scheduler chunks retain their exact owners until consumption.
class CommandRecorder {
public:
	CommandRecorder(vk::CommandBuffer direct): m_direct(direct) {}
	CommandRecorder(CommandScheduler& scheduler, vk::CommandBuffer direct)
	    : m_scheduler(&scheduler), m_direct(direct) {}
	bool Threaded() const { return m_scheduler && m_scheduler->Threaded(); }
	bool operator==(std::nullptr_t) const { return !Threaded() && m_direct == nullptr; }
	operator vk::CommandBuffer() const { return Threaded() ? m_scheduler->DrainRecording() : m_direct; }
	void bindPipeline(vk::PipelineBindPoint point, vk::Pipeline pipeline) const { Run([=](auto c){ c.bindPipeline(point,pipeline); }); }
	void draw(uint32_t n,uint32_t instances,uint32_t first,uint32_t instance) const { Run([=](auto c){ c.draw(n,instances,first,instance); }); }
	void drawIndexed(uint32_t n,uint32_t instances,uint32_t first,int32_t offset,uint32_t instance) const { Run([=](auto c){ c.drawIndexed(n,instances,first,offset,instance); }); }
	void drawIndirect(vk::Buffer b,vk::DeviceSize offset,uint32_t count,uint32_t stride) const { Run([=](auto c){ c.drawIndirect(b,offset,count,stride); }); }
	void drawIndexedIndirect(vk::Buffer b,vk::DeviceSize offset,uint32_t count,uint32_t stride) const { Run([=](auto c){ c.drawIndexedIndirect(b,offset,count,stride); }); }
	void drawMeshTasksEXT(uint32_t x,uint32_t y,uint32_t z) const { Run([=](auto c){ c.drawMeshTasksEXT(x,y,z); }); }
	void drawMeshTasksIndirectEXT(vk::Buffer b,vk::DeviceSize offset,uint32_t count,uint32_t stride) const { Run([=](auto c){ c.drawMeshTasksIndirectEXT(b,offset,count,stride); }); }
	void dispatch(uint32_t x,uint32_t y,uint32_t z) const { Run([=](auto c){ c.dispatch(x,y,z); }); }
	void dispatchIndirect(vk::Buffer b,vk::DeviceSize offset) const { Run([=](auto c){ c.dispatchIndirect(b,offset); }); }
	void bindIndexBuffer(vk::Buffer b,vk::DeviceSize offset,vk::IndexType type) const { Run([=](auto c){ c.bindIndexBuffer(b,offset,type); }); }
	void bindVertexBuffers2(uint32_t first,uint32_t count,const vk::Buffer* buffers,
	                        const vk::DeviceSize* offsets,const vk::DeviceSize* sizes,const vk::DeviceSize* strides) const {
		if(!Threaded()) { m_direct.bindVertexBuffers2(first,count,buffers,offsets,sizes,strides); return; }
		const auto* b=m_scheduler->Stash(buffers,count); const auto* o=m_scheduler->Stash(offsets,count);
		const auto* s=m_scheduler->Stash(sizes,sizes?count:0); const auto* d=m_scheduler->Stash(strides,strides?count:0);
		Run([=](auto c){ c.bindVertexBuffers2(first,count,b,o,s,d); });
	}
	void pushConstants(vk::PipelineLayout layout,vk::ShaderStageFlags stages,uint32_t offset,uint32_t size,const void* values) const {
		if(!Threaded()) { m_direct.pushConstants(layout,stages,offset,size,values); return; }
		const auto* data=m_scheduler->Stash(static_cast<const std::byte*>(values),size);
		Run([=](auto c){ c.pushConstants(layout,stages,offset,size,data); });
	}
	void bindDescriptorSets(vk::PipelineBindPoint point,vk::PipelineLayout layout,uint32_t first,uint32_t count,
	                        const vk::DescriptorSet* sets,uint32_t dynamic_count,const uint32_t* dynamic) const {
		if(!Threaded()) { m_direct.bindDescriptorSets(point,layout,first,count,sets,dynamic_count,dynamic); return; }
		const auto* s=m_scheduler->Stash(sets,count); const auto* d=m_scheduler->Stash(dynamic,dynamic_count);
		Run([=](auto c){ c.bindDescriptorSets(point,layout,first,count,s,dynamic_count,d); });
	}
	void pushDescriptorSetKHR(vk::PipelineBindPoint point,vk::PipelineLayout layout,uint32_t set,uint32_t count,const vk::WriteDescriptorSet* writes) const {
		if(!Threaded()) { m_direct.pushDescriptorSetKHR(point,layout,set,count,writes); return; }
		const auto* copy=StashWrites(writes,count); Run([=](auto c){ c.pushDescriptorSetKHR(point,layout,set,count,copy); });
	}
	template<typename Writes> void pushDescriptorSetKHR(vk::PipelineBindPoint point,vk::PipelineLayout layout,uint32_t set,const Writes& writes) const {
		pushDescriptorSetKHR(point,layout,set,static_cast<uint32_t>(std::size(writes)),std::data(writes));
	}
	void updateAndBindDescriptorSet(vk::Device device,vk::PipelineBindPoint point,vk::PipelineLayout layout,
	                                vk::DescriptorSet set,uint32_t count,const vk::WriteDescriptorSet* writes) const {
		const auto* copy=Threaded()?StashWrites(writes,count):writes;
		Run([=](auto c){ device.updateDescriptorSets(count,copy,0,nullptr); c.bindDescriptorSets(point,layout,0,1,&set,0,nullptr); });
	}
	void pipelineBarrier(vk::PipelineStageFlags source,vk::PipelineStageFlags destination,vk::DependencyFlags flags,
	                     uint32_t mc,const vk::MemoryBarrier* memory,uint32_t bc,const vk::BufferMemoryBarrier* buffers,
	                     uint32_t ic,const vk::ImageMemoryBarrier* images) const {
		if(!Threaded()) { m_direct.pipelineBarrier(source,destination,flags,mc,memory,bc,buffers,ic,images); return; }
		RequireNoNext(memory,mc); RequireNoNext(buffers,bc); RequireNoNext(images,ic);
		const auto* m=m_scheduler->Stash(memory,mc); const auto* b=m_scheduler->Stash(buffers,bc); const auto* i=m_scheduler->Stash(images,ic);
		Run([=](auto c){ c.pipelineBarrier(source,destination,flags,mc,m,bc,b,ic,i); });
	}
	void pipelineBarrier(vk::PipelineStageFlags source,vk::PipelineStageFlags destination,vk::DependencyFlags flags,
	                     vk::ArrayProxy<const vk::MemoryBarrier> m,vk::ArrayProxy<const vk::BufferMemoryBarrier> b,
	                     vk::ArrayProxy<const vk::ImageMemoryBarrier> i) const {
		pipelineBarrier(source,destination,flags,m.size(),m.data(),b.size(),b.data(),i.size(),i.data());
	}
	void pipelineBarrier2(const vk::DependencyInfo& info) const {
		if(!Threaded()) { m_direct.pipelineBarrier2(info); return; }
		EXIT_IF(info.pNext); RequireNoNext(info.pMemoryBarriers,info.memoryBarrierCount);
		RequireNoNext(info.pBufferMemoryBarriers,info.bufferMemoryBarrierCount); RequireNoNext(info.pImageMemoryBarriers,info.imageMemoryBarrierCount);
		auto copy=info; copy.pMemoryBarriers=m_scheduler->Stash(info.pMemoryBarriers,info.memoryBarrierCount);
		copy.pBufferMemoryBarriers=m_scheduler->Stash(info.pBufferMemoryBarriers,info.bufferMemoryBarrierCount);
		copy.pImageMemoryBarriers=m_scheduler->Stash(info.pImageMemoryBarriers,info.imageMemoryBarrierCount);
		Run([copy](auto c){ c.pipelineBarrier2(copy); });
	}
	void copyBuffer(vk::Buffer s,vk::Buffer d,uint32_t count,const vk::BufferCopy* regions) const {
		const auto* r=Threaded()?m_scheduler->Stash(regions,count):regions; Run([=](auto c){ c.copyBuffer(s,d,count,r); });
	}
	void copyBuffer(vk::Buffer s,vk::Buffer d,const vk::BufferCopy& r) const { copyBuffer(s,d,1,&r); }
	void updateBuffer(vk::Buffer b,vk::DeviceSize offset,vk::DeviceSize size,const void* data) const {
		const auto* copy=Threaded()?m_scheduler->Stash(static_cast<const std::byte*>(data),size):static_cast<const std::byte*>(data);
		Run([=](auto c){ c.updateBuffer(b,offset,size,copy); });
	}
	void fillBuffer(vk::Buffer b,vk::DeviceSize offset,vk::DeviceSize size,uint32_t value) const { Run([=](auto c){ c.fillBuffer(b,offset,size,value); }); }
	void copyBufferToImage(vk::Buffer s,vk::Image d,vk::ImageLayout layout,uint32_t count,const vk::BufferImageCopy* regions) const {
		const auto* r=Threaded()?m_scheduler->Stash(regions,count):regions; Run([=](auto c){ c.copyBufferToImage(s,d,layout,count,r); });
	}
	void copyBufferToImage(vk::Buffer s,vk::Image d,vk::ImageLayout layout,const vk::BufferImageCopy& r) const { copyBufferToImage(s,d,layout,1,&r); }
	void copyImageToBuffer(vk::Image s,vk::ImageLayout layout,vk::Buffer d,uint32_t count,const vk::BufferImageCopy* regions) const {
		const auto* r=Threaded()?m_scheduler->Stash(regions,count):regions; Run([=](auto c){ c.copyImageToBuffer(s,layout,d,count,r); });
	}
	void copyImageToBuffer(vk::Image s,vk::ImageLayout layout,vk::Buffer d,const vk::BufferImageCopy& r) const { copyImageToBuffer(s,layout,d,1,&r); }
	void copyImage(vk::Image s,vk::ImageLayout sl,vk::Image d,vk::ImageLayout dl,uint32_t count,const vk::ImageCopy* regions) const {
		const auto* r=Threaded()?m_scheduler->Stash(regions,count):regions; Run([=](auto c){ c.copyImage(s,sl,d,dl,count,r); });
	}
	void copyImage(vk::Image s,vk::ImageLayout sl,vk::Image d,vk::ImageLayout dl,const vk::ImageCopy& r) const { copyImage(s,sl,d,dl,1,&r); }
	void resolveImage(vk::Image s,vk::ImageLayout sl,vk::Image d,vk::ImageLayout dl,const vk::ImageResolve& r) const {
		Run([=](auto c){ c.resolveImage(s,sl,d,dl,1,&r); });
	}
	void clearColorImage(vk::Image image,vk::ImageLayout layout,const vk::ClearColorValue* color,uint32_t count,const vk::ImageSubresourceRange* ranges) const {
		const auto value=*color; const auto* r=Threaded()?m_scheduler->Stash(ranges,count):ranges;
		Run([=](auto c){ c.clearColorImage(image,layout,&value,count,r); });
	}
	void clearDepthStencilImage(vk::Image image,vk::ImageLayout layout,const vk::ClearDepthStencilValue* clear,uint32_t count,const vk::ImageSubresourceRange* ranges) const {
		const auto value=*clear; const auto* r=Threaded()?m_scheduler->Stash(ranges,count):ranges;
		Run([=](auto c){ c.clearDepthStencilImage(image,layout,&value,count,r); });
	}
	void beginRendering(const vk::RenderingInfo& info) const {
		if(!Threaded()) { m_direct.beginRendering(info); return; }
		EXIT_IF(info.pNext); RequireNoNext(info.pColorAttachments,info.colorAttachmentCount);
		RequireNoNext(info.pDepthAttachment,info.pDepthAttachment?1:0); RequireNoNext(info.pStencilAttachment,info.pStencilAttachment?1:0);
		auto copy=info; copy.pColorAttachments=m_scheduler->Stash(info.pColorAttachments,info.colorAttachmentCount);
		copy.pDepthAttachment=m_scheduler->Stash(info.pDepthAttachment,info.pDepthAttachment?1:0);
		copy.pStencilAttachment=m_scheduler->Stash(info.pStencilAttachment,info.pStencilAttachment?1:0);
		Run([copy](auto c){ c.beginRendering(copy); });
	}
	void beginRendering(const vk::RenderingInfo* info) const { beginRendering(*info); }
	void endRendering() const { Run([](auto c){ c.endRendering(); }); }
	void beginConditionalRenderingEXT(const vk::ConditionalRenderingBeginInfoEXT* info) const {
		EXIT_IF(info == nullptr || info->pNext != nullptr);
		const auto copy = *info;
		Run([copy](auto c){ c.beginConditionalRenderingEXT(&copy); });
	}
	void beginConditionalRenderingEXT(const vk::ConditionalRenderingBeginInfoEXT& info) const { beginConditionalRenderingEXT(&info); }
	void endConditionalRenderingEXT() const { Run([](auto c){ c.endConditionalRenderingEXT(); }); }
	void clearAttachments(uint32_t ac,const vk::ClearAttachment* attachments,uint32_t rc,const vk::ClearRect* rects) const {
		const auto* a=Threaded()?m_scheduler->Stash(attachments,ac):attachments;
		const auto* r=Threaded()?m_scheduler->Stash(rects,rc):rects;
		Run([=](auto c){ c.clearAttachments(ac,a,rc,r); });
	}
	void setCheckpointNV(const void* marker) const { Run([=](auto c){ c.setCheckpointNV(marker); }); }
	void writeBufferMarkerAMD(vk::PipelineStageFlagBits stage,vk::Buffer b,vk::DeviceSize offset,uint32_t marker) const {
		Run([=](auto c){ c.writeBufferMarkerAMD(stage,b,offset,marker); });
	}
	void writeTimestamp(vk::PipelineStageFlagBits stage,vk::QueryPool p,uint32_t query) const { Run([=](auto c){ c.writeTimestamp(stage,p,query); }); }
	void setViewportWithCount(uint32_t count,const vk::Viewport* values) const { const auto* v=Threaded()?m_scheduler->Stash(values,count):values; Run([=](auto c){ c.setViewportWithCount(count,v); }); }
	void setViewport(uint32_t first,uint32_t count,const vk::Viewport* values) const { const auto* v=Threaded()?m_scheduler->Stash(values,count):values; Run([=](auto c){ c.setViewport(first,count,v); }); }
	void setScissorWithCount(uint32_t count,const vk::Rect2D* values) const { const auto* v=Threaded()?m_scheduler->Stash(values,count):values; Run([=](auto c){ c.setScissorWithCount(count,v); }); }
	void setScissor(uint32_t first,uint32_t count,const vk::Rect2D* values) const { const auto* v=Threaded()?m_scheduler->Stash(values,count):values; Run([=](auto c){ c.setScissor(first,count,v); }); }
	void setLineWidth(float v) const { Run([=](auto c){ c.setLineWidth(v); }); }
	void setBlendConstants(const float* v) const { const std::array<float,4> copy{v[0],v[1],v[2],v[3]}; Run([=](auto c){ c.setBlendConstants(copy.data()); }); }
	void setCullMode(vk::CullModeFlags v) const { Run([=](auto c){ c.setCullMode(v); }); }
	void setFrontFace(vk::FrontFace v) const { Run([=](auto c){ c.setFrontFace(v); }); }
	void setDepthBoundsTestEnable(vk::Bool32 v) const { Run([=](auto c){ c.setDepthBoundsTestEnable(v); }); }
	void setDepthBounds(float a,float b) const { Run([=](auto c){ c.setDepthBounds(a,b); }); }
	void setDepthTestEnable(vk::Bool32 v) const { Run([=](auto c){ c.setDepthTestEnable(v); }); }
	void setDepthWriteEnable(vk::Bool32 v) const { Run([=](auto c){ c.setDepthWriteEnable(v); }); }
	void setDepthCompareOp(vk::CompareOp v) const { Run([=](auto c){ c.setDepthCompareOp(v); }); }
	void setDepthBiasEnable(vk::Bool32 v) const { Run([=](auto c){ c.setDepthBiasEnable(v); }); }
	void setDepthBias(float a,float b,float d) const { Run([=](auto c){ c.setDepthBias(a,b,d); }); }
	void setStencilTestEnable(vk::Bool32 v) const { Run([=](auto c){ c.setStencilTestEnable(v); }); }
	void setStencilOp(vk::StencilFaceFlags f,vk::StencilOp fail,vk::StencilOp pass,vk::StencilOp depth,vk::CompareOp compare) const { Run([=](auto c){ c.setStencilOp(f,fail,pass,depth,compare); }); }
	void setStencilCompareMask(vk::StencilFaceFlags f,uint32_t v) const { Run([=](auto c){ c.setStencilCompareMask(f,v); }); }
	void setStencilWriteMask(vk::StencilFaceFlags f,uint32_t v) const { Run([=](auto c){ c.setStencilWriteMask(f,v); }); }
	void setStencilReference(vk::StencilFaceFlags f,uint32_t v) const { Run([=](auto c){ c.setStencilReference(f,v); }); }
	void setColorWriteEnableEXT(uint32_t count,const vk::Bool32* values) const { const auto* v=Threaded()?m_scheduler->Stash(values,count):values; Run([=](auto c){ c.setColorWriteEnableEXT(count,v); }); }
	void setAttachmentFeedbackLoopEnableEXT(vk::ImageAspectFlags v) const { Run([=](auto c){ c.setAttachmentFeedbackLoopEnableEXT(v); }); }
private:
	template<typename F> void Run(F&& fn) const { if(Threaded()) m_scheduler->Record(std::forward<F>(fn)); else fn(m_direct); }
	template<typename T> static void RequireNoNext(const T* items,uint32_t count) { EXIT_IF(count && !items); for(uint32_t i=0;i<count;i++) EXIT_IF(items[i].pNext); }
	const vk::WriteDescriptorSet* StashWrites(const vk::WriteDescriptorSet* writes,uint32_t count) const {
		RequireNoNext(writes,count); auto* copy=const_cast<vk::WriteDescriptorSet*>(m_scheduler->Stash(writes,count));
		for(uint32_t i=0;i<count;i++) {
			auto& w=copy[i]; w.pImageInfo=m_scheduler->Stash(w.pImageInfo,w.pImageInfo?w.descriptorCount:0);
			w.pBufferInfo=m_scheduler->Stash(w.pBufferInfo,w.pBufferInfo?w.descriptorCount:0);
			w.pTexelBufferView=m_scheduler->Stash(w.pTexelBufferView,w.pTexelBufferView?w.descriptorCount:0);
		}
		return copy;
	}
	CommandScheduler* m_scheduler=nullptr;
	vk::CommandBuffer m_direct=nullptr;
};
} // namespace Libs::Graphics
#endif
