#pragma once
// The renderer's Vulkan objects, shared by its src/render/vk_*.cpp files only.
#include "render/renderer.hpp"

#include <map>
#include <vulkan/vulkan.h>

struct GpuBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* mapped = nullptr; // host-visible buffers stay mapped
};

struct GpuImage {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkImageView depth_view = VK_NULL_HANDLE; // depth-stencil images: the depth aspect, sampled
    VkFormat format = VK_FORMAT_UNDEFINED;
    int width = 0, height = 0, levels = 1;
};

struct GpuMesh {
    GpuBuffer vertices;
    GpuBuffer indices;
};

// the render passes the pipelines are made for
enum class PassKind : std::uint8_t { scene, screen };

// the composite's own programs (vk_composite.cpp), after the game's
// (post: the port's FXAA and brightness over the 3D, post.frag)
enum class ScreenProgram : std::uint8_t { blur, dof, dofblur, copy, gamma, post, count };

// images the composite binds, by texture id below zero
enum : int {
    texture_blur320 = -10,
    texture_blur160 = -11,
    texture_depth = -12,
    texture_wave = -13,
    texture_ramp = -14,
    texture_final = -15,
    texture_post = -16,
};

struct RendererState {
    // the screen adjust this frame, 640x480 screen pixels (FrameSetup::screen_shift): the 2D's
    // clips move with the 2D it shifted
    float screen_shift[2] = {0, 0};
    SDL_Window* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    std::uint32_t family = 0;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkSemaphore finished = VK_NULL_HANDLE;
    VkDeviceSize uniform_alignment = 256;
    float max_anisotropy = 0; // 0 when the device can't
    RenderOptions options;    // what the targets and samplers were made for

    // the window's swapchain
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swap_format = VK_FORMAT_UNDEFINED;
    VkExtent2D swap_extent{};
    std::vector<VkImage> swap_images;
    bool swap_stale = true;

    // the targets, at the window's size: the scene (0x223) and its depth-stencil (0x224), the
    // downsamples (blur320_texture, blur160_texture) at a half and a quarter, the final target
    // (the back buffer) and the output the gamma ramp writes
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    GpuImage scene, depth, blur320, blur160, final, output;
    // the composite's own target when the 3D takes a post pass (FXAA, brightness:
    // vk_composite.cpp), with a late framebuffer of its own for the cockpit's meshes
    GpuImage post;
    VkFramebuffer post_framebuffer = VK_NULL_HANDLE;
    VkFramebuffer post_late_framebuffer = VK_NULL_HANDLE;
    VkRenderPass scene_pass = VK_NULL_HANDLE;
    VkRenderPass screen_pass = VK_NULL_HANDLE;
    VkFramebuffer scene_framebuffer = VK_NULL_HANDLE;
    VkFramebuffer blur320_framebuffer = VK_NULL_HANDLE;
    VkFramebuffer blur160_framebuffer = VK_NULL_HANDLE;
    VkFramebuffer final_framebuffer = VK_NULL_HANDLE;
    VkFramebuffer output_framebuffer = VK_NULL_HANDLE;
    VkRenderPass late_pass = VK_NULL_HANDLE; // render pass 13: final + a cleared depth
    VkFramebuffer late_framebuffer = VK_NULL_HANDLE;
    // the backdrop 0x222 (vk_backdrop.cpp): the scene copied in before pass 5 or 6, and the
    // scene pass resumed over what it left (colour, depth and stencil loaded)
    GpuImage backdrop;
    // 0x223 as the last frame with 3D left it, for the big screens (mesh 0x80000), same size
    GpuImage scene_last;
    VkRenderPass resume_pass = VK_NULL_HANDLE;
    VkFramebuffer resume_framebuffer = VK_NULL_HANDLE;

    // the heat haze's wave texture (64x64 V8U8) and the gamma ramp (256x1), rewritten each frame
    GpuImage wave, ramp;
    GpuBuffer staging; // their bytes, and the dynamic quads

    // shading: the binding layout of docs/shaders.md
    VkDescriptorSetLayout frame_layout = VK_NULL_HANDLE;   // set 0: Frame
    VkDescriptorSetLayout draw_layout = VK_NULL_HANDLE;    // set 1: the per-draw ring, storage
    VkDescriptorSetLayout texture_layout = VK_NULL_HANDLE; // set 2: four stages
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet frame_set = VK_NULL_HANDLE;
    VkDescriptorSet draw_set = VK_NULL_HANDLE;
    GpuBuffer frame_uniform;
    GpuBuffer draw_uniforms; // the per-draw ring
    GpuBuffer storage;       // set 1 binding 1 (particles and sprites, later)
    GpuBuffer bone_zero;     // one float 0: the fighters' bone selector
    GpuBuffer screen_quads;  // the composite's fixed quads
    std::map<std::string, VkShaderModule> shaders;
    std::map<std::uint64_t, VkPipeline> pipelines;
    std::map<int, VkSampler> samplers;
    std::map<std::vector<int>, VkDescriptorSet> texture_sets;

    // uploads recorded into one command buffer, submitted together before the next frame
    VkCommandBuffer uploads = VK_NULL_HANDLE;
    std::vector<GpuBuffer> upload_staging;
    VkDeviceSize upload_bytes = 0;

    std::vector<GpuMesh> meshes;
    std::vector<GpuImage> textures;
    // freed handles (their slots reused by the next uploads), and what they held, destroyed once
    // the frame in flight is done (vk_release_freed); the texture set cache is rebuilt then
    std::vector<int> free_meshes, free_textures;
    std::vector<GpuMesh> freed_meshes;
    std::vector<GpuImage> freed_textures;
    // who each slot belongs to (renderer_set_owner), and the owner of uploads made now
    std::vector<int> mesh_owner, texture_owner;
    int owner = 0;
    // the capture 0x225 (frame_to_texture): a texture the final target is copied into at the end
    // of each frame while capture_on, -1 until asked for
    int capture_texture = -1;
    bool capture_on = false;

    // the depth probe (renderer_depth_probe): the point to read in the next frame, the read
    GpuBuffer probe;
    float probe_at[2] = {-1, -1};
    bool probe_recorded = false;
    float probe_value = 0;

    // the 2D overlay (vk_sprites.cpp)
    struct SpriteState* sprites = nullptr;
};

// vk_device.cpp
bool vk_check(VkResult result, const char* what);
std::uint32_t vk_memory_type(RendererState& vk, std::uint32_t bits, VkMemoryPropertyFlags flags);
bool vk_create_buffer(RendererState& vk, VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags flags, GpuBuffer& out);
// host-visible, coherent and mapped
bool vk_create_mapped_buffer(RendererState& vk, VkDeviceSize size, VkBufferUsageFlags usage,
                             GpuBuffer& out);
void vk_destroy_buffer(RendererState& vk, GpuBuffer& buffer);
bool vk_create_image(RendererState& vk, int width, int height, int levels, VkFormat format,
                     VkImageUsageFlags usage, VkImageAspectFlags aspect, GpuImage& out);
void vk_destroy_image(RendererState& vk, GpuImage& image);
// a one-off command buffer, run and waited for
VkCommandBuffer vk_begin_once(RendererState& vk);
bool vk_end_once(RendererState& vk, VkCommandBuffer command);
// the pending uploads' command buffer (begun on first use), a staging buffer kept until they run,
// and the run: one submit and wait for everything uploaded since the last
VkCommandBuffer vk_upload_command(RendererState& vk);
void vk_upload_keep(RendererState& vk, GpuBuffer& staging);
bool vk_flush_uploads(RendererState& vk);
// vk_resources.cpp: destroys what renderer_free_* handed back, after the last frame and the
// pending uploads are done; the texture sets made again when a texture went
void vk_release_freed(RendererState& vk);
void vk_barrier(VkCommandBuffer command, VkImage image, VkImageAspectFlags aspect,
                VkImageLayout from, VkImageLayout to, int levels = 1);

// vk_target.cpp: swapchain and targets at the window's size
bool vk_fit_window(RendererState& vk, Renderer& renderer);
void vk_destroy_targets(RendererState& vk);
// every render pass's two external dependencies, the same for all so any pipeline and
// framebuffer fit any pass of their attachments (render pass compatibility)
void vk_pass_dependencies(VkSubpassDependency (&out)[2]);

// vk_backdrop.cpp: the backdrop and the resume pass at the targets' size (after the scene's), and
// their end; the copy inside the scene pass: the pass ended, the scene's 640x480 screen (its
// fitted rectangle) copied over the backdrop, the pass begun again loading what it had
bool vk_create_backdrop(RendererState& vk);
void vk_destroy_backdrop(RendererState& vk);
void vk_record_backdrop(RendererState& vk, VkCommandBuffer command);
// the scene's 640x480 screen over scene_last, outside any pass, before the composite reads it
void vk_record_scene_last(RendererState& vk, VkCommandBuffer command);
// the final target (before the gamma ramp) over the capture texture, when capturing
void vk_record_capture(RendererState& vk, VkCommandBuffer command);
// vk_resources.cpp: an image as a texture handle (a freed one first) belonging to owner
int vk_keep_texture(RendererState& vk, const GpuImage& image, int owner);

// vk_pipeline.cpp
bool vk_create_shading(RendererState& vk);
void vk_destroy_shading(RendererState& vk);
VkShaderModule vk_shader(RendererState& vk, const std::string& name);
// a pipeline's states; program is a Program, or screen_program_base + a ScreenProgram (past the
// last Program: a fixed 16 had the blur run effopt's pair once effopt took Program 16)
constexpr int screen_program_base = static_cast<int>(Program::count);
struct PipelineKey {
    int program = 0;
    Blend blend = Blend::none;
    bool depth_test = false;
    bool depth_write = false;
    Cull cull = Cull::none;
    Stencil stencil = Stencil::off;
    PassKind pass = PassKind::screen;
};
VkPipeline vk_pipeline_for(RendererState& vk, const PipelineKey& key);
VkSampler vk_sampler(RendererState& vk, Address u, Address v, bool point);
// the set for four stages; ids below zero are the renderer's own images
VkDescriptorSet vk_texture_set(RendererState& vk, const StageTexture* stages);
// the descriptor sets that point at targets, made again after a resize
void vk_reset_descriptors(RendererState& vk);

// vk_composite.cpp: the pass 11 composite, the gamma ramp
bool vk_create_composite(RendererState& vk);
void vk_write_dynamic_textures(RendererState& vk, VkCommandBuffer command,
                               const FrameSetup& setup);
// into the final target, or (post) into the post target for vk_record_post
void vk_record_composite(RendererState& vk, VkCommandBuffer command, const FrameSetup& setup,
                         std::uint32_t& uniform_cursor, bool post);
// whether the frame's 3D takes the post pass: FXAA on, or a brightness other than 100
bool vk_post_wanted(const RendererState& vk);
// the post target through FXAA and the brightness curve into the final target
void vk_record_post(RendererState& vk, VkCommandBuffer command, std::uint32_t& uniform_cursor);
// a frame without 3D: pass_open_effects composites only when passes 1..10 drew (rp->scene_3d), so
// the scene target (the clear and pass 0) goes to the final target as it is
void vk_record_scene_copy(RendererState& vk, VkCommandBuffer command);
void vk_record_gamma(RendererState& vk, VkCommandBuffer command, std::uint32_t& uniform_cursor);
// the shadow quad of pass 7, inside the scene pass
void vk_record_shadow_quad(RendererState& vk, VkCommandBuffer command, const DrawItem& item,
                           const FrameSetup& setup);
