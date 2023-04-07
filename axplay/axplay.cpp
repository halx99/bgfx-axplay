/*
 * Copyright 2011-2023 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#include <bx/uint32_t.h>
#include "common.h"
#include "bgfx_utils.h"
#include "imgui/imgui.h"
#include "axplay.h"
#include "media/MediaEngine.h"
#include "yasio/detail/byte_buffer.hpp"
#include "glm/glm.hpp"

#include "ImGuiFileDialog/ImGuiFileDialog.h"
#include "ImGuiFileDialog/CustomFont.h"

namespace
{
	class TSRefCountedObject {

	public:
		TSRefCountedObject() { _refCount = 1; }
		virtual ~TSRefCountedObject() {}

		void retain() { ++_refCount; }

		void release()
		{
			--_refCount;
			if (_refCount == 0)
				delete this;
		}

	protected:
		std::atomic_uint _refCount;
	};

	class TSRefByteBuffer : public TSRefCountedObject, public yasio::byte_buffer
	{
	public:
		template <typename _Iter>
		TSRefByteBuffer(_Iter first, _Iter last, std::true_type /*fit*/) : yasio::byte_buffer(first, last, std::true_type{}) {}

		TSRefByteBuffer* incRef() {
			this->retain();
			return this;
		}
	};


	struct PosTexColorVertex
	{
		float m_x;
		float m_y;
		float m_z;
		float t_x;
		float t_y;

		uint32_t m_abgr;

		static void init()
		{
			ms_decl
				.begin()
				.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
				.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float, true)
				.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
				.end();
		};

		static bgfx::VertexLayout ms_decl;
	};

	bgfx::VertexLayout PosTexColorVertex::ms_decl;

	static PosTexColorVertex s_vertices[] =
	{
		{-1.0f,  1.0f,  0.0f, 0.0f, 0.0f, 0xffffffff }, // LT
		{ 1.0f,  1.0f,  0.0f, 1.0f, 0.0f, 0xffffffff }, // RT
		{-1.0f, -1.0f,  0.0f, 0.0f, 1.0f, 0xffffffff }, // LB
		{ 1.0f, -1.0f,  0.0f, 1.0f, 1.0f, 0xffffffff }, // RB
	};

	static const uint16_t s_triList[] =
	{
		0, 1, 2, // 0
		1, 3, 2,
	};

	static auto s_meFactory = ax::CreatePlatformMediaEngineFactory();

	static auto s_YuvToRgbRec709Scaled = glm::mat4{ // 709Scaled
		glm::vec4{1.16438356f, 1.16438356f,   1.16438356f,  0.0627451017f},
		glm::vec4{0.00000000f, -0.213237017f, 2.11241937f,  0.501960814f},
		glm::vec4{1.79265225f, -0.533004045f, 0.00000000f,  0.501960814f},
		glm::vec4{0.0627451017f, 0.501960814f,0.501960814f, 0.0f} // YUVOffset8Bits: 16/255.0f, 128/255.0f, 128/255.0f
	};

	class ExampleHelloWorld : public entry::AppI
	{
		// bgfx::VertexBufferHandle m_vbh;
		// bgfx::IndexBufferHandle m_ibh;
		bgfx::ProgramHandle m_program;
		bgfx::ProgramHandle m_programNV12;
		bgfx::ProgramHandle m_programYUY2;
		bgfx::ProgramHandle m_programBGRA;
		bgfx::TextureHandle m_spriteTexture{ bgfx::kInvalidHandle };
		bgfx::UniformHandle m_spriteTextureLoc{ bgfx::kInvalidHandle };
		bgfx::TextureHandle m_lumaTexture{ bgfx::kInvalidHandle };
		bgfx::UniformHandle m_lumaTextureLoc{ bgfx::kInvalidHandle };
		bgfx::TextureHandle m_chromaTexture{ bgfx::kInvalidHandle };
		bgfx::UniformHandle m_chromaTextureLoc{ bgfx::kInvalidHandle };
		bgfx::UniformHandle m_colorTransformLoc{ bgfx::kInvalidHandle };
		bgfx::UniformHandle m_paramsLoc{ bgfx::kInvalidHandle };
		ax::MediaEngine* m_me;
		ax::MEVideoPixelDesc m_vpd;
		int m_videoFrameCount = 0;
		ax::MEIntPoint m_videoSize;
		ax::MEIntPoint m_spriteSize;
		std::string m_lastVideoDir = ".";
		float m_dpiScale = 1.0f;
	public:
		ExampleHelloWorld(const char* _name, const char* _description, const char* _url)
			: entry::AppI(_name, _description, _url)
		{
			m_me = s_meFactory->CreateMediaEngine();
		}
		~ExampleHelloWorld()
		{
			s_meFactory->DestroyMediaEngine(m_me);
		}

		void init(int32_t _argc, const char* const* _argv, uint32_t _width, uint32_t _height) override
		{
			Args args(_argc, _argv);

			m_width = _width;
			m_height = _height;
			m_debug = BGFX_DEBUG_TEXT;
			m_reset = BGFX_RESET_VSYNC;

			bgfx::Init init;
			// NV12 render with bgfx
			// Windows: only OpenGL backend works well
			init.type = args.m_type;
			init.vendorId = args.m_pciId;
			init.platformData.nwh = entry::getNativeWindowHandle(entry::kDefaultWindowHandle);
			init.platformData.ndt = entry::getNativeDisplayHandle();
			init.resolution.width = m_width;
			init.resolution.height = m_height;
			init.resolution.reset = m_reset;
			bgfx::init(init);

			// Enable debug text.
			bgfx::setDebug(m_debug);

			// Set view 0 clear state.
			bgfx::setViewClear(0
				, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
				, 0x303030ff
				, 1.0f
				, 0
			);


			// Create vertex stream declaration.
			PosTexColorVertex::init();

#if 0
			// Create static vertex buffer.
			m_vbh = bgfx::createVertexBuffer(
				// Static data can be passed with bgfx::makeRef
				bgfx::makeRef(s_vertices, sizeof(s_vertices))
				, PosTexColorVertex::ms_decl, BGFX_BUFFER_COMPUTE_WRITE
			);

			// Create static index buffer for triangle list rendering.
			m_ibh = bgfx::createIndexBuffer(
				// Static data can be passed with bgfx::makeRef
				bgfx::makeRef(s_triList, sizeof(s_triList))
			);
#endif

			// Create program from shaders.
			m_program = loadProgram("vs_sprite", "fs_sprite");
			m_spriteTextureLoc = bgfx::createUniform("spriteTexture", bgfx::UniformType::Sampler);
			bgfx::TextureInfo info{};
			m_spriteTexture = loadTexture(AXPLAY_LOGO, 0, 0, &info);
			m_spriteSize.x = info.width;
			m_spriteSize.y = info.height;

			// VideoTexture render
			m_lumaTextureLoc = bgfx::createUniform("u_tex0", bgfx::UniformType::Sampler);
			m_chromaTextureLoc = bgfx::createUniform("u_tex1", bgfx::UniformType::Sampler);

			m_colorTransformLoc = bgfx::createUniform("colorTransform", bgfx::UniformType::Mat4);
			m_paramsLoc = bgfx::createUniform("params", bgfx::UniformType::Vec4);

			// NV12
			m_programNV12 = loadProgram("vs_sprite", "fs_nv12");
			m_programYUY2 = loadProgram("vs_sprite", "fs_yuy2");
			m_programBGRA = loadProgram("vs_sprite", "fs_bgra");

			// Open Media
			m_me->SetAutoPlay(true);
			m_me->SetLoop(true);

			imguiCreate();

			//static const ImWchar icons_ranges[] = { ICON_MIN_IGFD, ICON_MAX_IGFD, 0 };
			//ImFontConfig icons_config; icons_config.MergeMode = true; icons_config.PixelSnapH = true;
			//ImGui::GetIO().Fonts->AddFontFromMemoryCompressedBase85TTF(FONT_ICON_BUFFER_NAME_IGFD, 48, &icons_config, icons_ranges);

			ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByFullName, "(Custom.+[.]h)", ImVec4(0.1f, 0.9f, 0.1f, 0.9f)); // use a regex
			ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByExtention, ".mp4", ImVec4(1.0f, 1.0f, 0.0f, 0.9f));
			ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByExtention, ".mkv", ImVec4(0.0f, 0.0f, 1.0f, 0.9f));
			ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByExtention, ".webm", ImVec4(1.0f, 0.0f, 1.0f, 0.9f));
			ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByExtention, ".png", ImVec4(0.0f, 1.0f, 1.0f, 0.9f), ICON_IGFD_FILE_PIC); // add an icon for the filter type
			ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByExtention, ".gif", ImVec4(0.0f, 1.0f, 0.5f, 0.9f), "[GIF]"); // add an text for a filter type
			ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByTypeDir, nullptr, ImVec4(0.5f, 1.0f, 0.9f, 0.9f), ICON_IGFD_FOLDER); // for all dirs
			//ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByTypeFile, "CMakeLists.txt", ImVec4(0.1f, 0.5f, 0.5f, 0.9f), ICON_IGFD_ADD);
			ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByFullName, "doc", ImVec4(0.9f, 0.2f, 0.0f, 0.9f), ICON_IGFD_FILE_PIC);
			//ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByTypeFile, nullptr, ImVec4(0.2f, 0.9f, 0.2f, 0.9f), ICON_IGFD_FILE); // for all link files
			ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByTypeDir | IGFD_FileStyleByTypeLink, nullptr, ImVec4(0.8f, 0.8f, 0.8f, 0.8f), ICON_IGFD_FOLDER); // for all link dirs
			ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByTypeFile | IGFD_FileStyleByTypeLink, nullptr, ImVec4(0.8f, 0.8f, 0.8f, 0.8f), ICON_IGFD_FILE); // for all link files
			ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByTypeDir | IGFD_FileStyleByContainedInFullName, ".git", ImVec4(0.9f, 0.2f, 0.0f, 0.9f), ICON_IGFD_BOOKMARK);
			ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByTypeFile | IGFD_FileStyleByContainedInFullName, ".git", ImVec4(0.5f, 0.8f, 0.5f, 0.9f), ICON_IGFD_SAVE);
		}

		virtual int shutdown() override
		{
			imguiDestroy();

			// bgfx::destroy(m_ibh);
			// bgfx::destroy(m_vbh);
			bgfx::destroy(m_program);
			if (bgfx::isValid(m_spriteTexture))
				bgfx::destroy(m_spriteTexture);
			bgfx::destroy(m_spriteTextureLoc);
			bgfx::destroy(m_colorTransformLoc);
			if (bgfx::isValid(m_lumaTexture))
				bgfx::destroy(m_lumaTexture);
			bgfx::destroy(m_lumaTextureLoc);
			if (bgfx::isValid(m_chromaTexture))
				bgfx::destroy(m_chromaTexture);
			bgfx::destroy(m_chromaTextureLoc);


			// Shutdown bgfx.
			bgfx::shutdown();

			return 0;
		}

		/*
		* keepSize:
		*   true: keepSize
		*   false: keepAspectRatio
		*/
		void updateTextureRect(bool keepSize, const ax::MEIntPoint& imageSize, const ax::MEIntPoint& videoSize)
		{/*
				* 	static PosTexColorVertex s_vertices[] =
	{
		{-1.0f,  1.0f,  0.0f, 0.0f, 0.0f, 0xffffffff }, // LT
		{ 1.0f,  1.0f,  0.0f, 1.0f, 0.0f, 0xffffffff }, // RT
		{-1.0f, -1.0f,  0.0f, 0.0f, 1.0f, 0xffffffff }, // LB
		{ 1.0f, -1.0f,  0.0f, 1.0f, 1.0f, 0xffffffff }, // RB
	};
			*/

			float wnorm = (float)videoSize.x / m_width;
			float hnorm = (float)videoSize.y / m_height;
			if (keepSize) {
				for (auto& item : s_vertices) {
					auto maxX = item.m_x > 0 ? wnorm : -wnorm;
					auto maxY = item.m_y > 0 ? hnorm : -hnorm;
					item.m_x = maxX;
					item.m_y = maxY;
				}
			}
			else { // keep ratio
				const auto aspectRatio = (std::min)((float)m_width / videoSize.x, (float)m_height / videoSize.y);
				for (auto& item : s_vertices) {
					auto maxX = item.m_x > 0 ? wnorm : -wnorm;
					auto maxY = item.m_y > 0 ? hnorm : -hnorm;
					item.m_x = aspectRatio * maxX;
					item.m_y = aspectRatio * maxY;
				}
			}

			float maxS = (float)videoSize.x / imageSize.x;
			float maxT = (float)videoSize.y / imageSize.y;

			// LT
			s_vertices[0].t_x = 0;
			s_vertices[0].t_y = 0;

			// RT
			s_vertices[1].t_x = maxS;
			s_vertices[1].t_y = 0;

			// LB
			s_vertices[2].t_x = 0;
			s_vertices[2].t_y = maxT;

			// RB
			s_vertices[3].t_x = maxS;
			s_vertices[3].t_y = maxT;
		}

		bool update() override
		{
			if (!entry::processEvents(m_width, m_height, m_debug, m_reset, &m_mouseState))
			{
				imguiBeginFrame(m_mouseState.m_mx
					, m_mouseState.m_my
					, (m_mouseState.m_buttons[entry::MouseButton::Left] ? IMGUI_MBUT_LEFT : 0)
					| (m_mouseState.m_buttons[entry::MouseButton::Right] ? IMGUI_MBUT_RIGHT : 0)
					| (m_mouseState.m_buttons[entry::MouseButton::Middle] ? IMGUI_MBUT_MIDDLE : 0)
					, m_mouseState.m_mz
					, uint16_t(m_width)
					, uint16_t(m_height)
				);

				showExampleDialog(this);

				ImGui::SetNextWindowPos(
					ImVec2(m_width - m_width / 5.0f - 10.0f, 10.0f)
					, ImGuiCond_FirstUseEver
				);
				ImGui::SetNextWindowSize(
					ImVec2(m_width / 5.0f, m_height * 0.75f)
					, ImGuiCond_FirstUseEver
				);

				ImGui::Begin("Control Panel"
					, NULL
					, 0
				);

				ImGui::Separator();

				auto igfd = ImGuiFileDialog::Instance();

				if (ImGui::Button("Play a Video File")) {

					const ImVec2 scaledDlgSize(600, 500);
					auto io = ImGui::GetIO();
					ImGui::SetNextWindowSize(scaledDlgSize);
					ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
					const char* filters = ".mp4,.mkv,.webm,.rmvb";
					const ImGuiFileDialogFlags flags = ImGuiFileDialogFlags_Modal;
					ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", ICON_IGFD_FOLDER_OPEN " Choose a File", filters, m_lastVideoDir, "", 1, nullptr, flags);
				}

				if (ImGui::Button("Play default"))
					m_me->Open("file://" AXPLAY_SAMPLE_VIDEO);

				ImVec2 minSize = ImVec2(0, 0);
				ImVec2 maxSize = ImVec2(FLT_MAX, FLT_MAX);

				//if (_UseWindowContraints)
				//{
				//	maxSize = ImVec2((float)display_w, (float)display_h) * 0.7f;
				//	minSize = maxSize * 0.25f;
				//}

				if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey",
					ImGuiWindowFlags_NoCollapse, minSize, maxSize))
				{
					if (ImGuiFileDialog::Instance()->IsOk())
					{
						m_lastVideoDir = igfd->GetCurrentPath();
						const auto fileUriPrefix = "file://"sv;
						auto filePathName = ImGuiFileDialog::Instance()->GetFilePathName();
						filePathName.insert(filePathName.begin(), fileUriPrefix.data(), fileUriPrefix.data() + fileUriPrefix.length());
						if (!m_me->Open(filePathName))
							m_me->Open("file://" AXPLAY_SAMPLE_VIDEO);
					}
					ImGuiFileDialog::Instance()->Close();
				}

				ImGui::End();

				imguiEndFrame();

				// Set view 0 default viewport.
				bgfx::setViewRect(0, 0, 0, uint16_t(m_width), uint16_t(m_height));

				// This dummy draw call is here to make sure that view 0 is cleared
				// if no other draw calls are submitted to view 0.
				bgfx::touch(0);


				/* render video in default view */
				renderVideoSample(0);

				// Advance to next frame. Rendering thread will be kicked to
				// process submitted rendering primitives.
				bgfx::frame();

				return true;
			}

			return false;
		}

		void renderVideoSample(bgfx::ViewId viewId) {
			if (m_width == 0 || m_height == 0) return;

			/*-------------- start of video texture render --------*/
			uint64_t state = 0
				| BGFX_STATE_WRITE_R
				| BGFX_STATE_WRITE_G
				| BGFX_STATE_WRITE_B
				| BGFX_STATE_WRITE_A
				| UINT64_C(0);

			bgfx::ProgramHandle* currProgram = &m_program;

			if (m_me->GetState() == ax::MEMediaState::Playing) {
				bool transferred = m_me->TransferVideoFrame([this](const ax::MEVideoFrame& frame) mutable {

					++m_videoFrameCount;

					bool needsRecreate = !frame._vpd.equals(m_vpd);

					auto& bufferDim = frame._vpd._dim;

					const unsigned int lumaSamplerFilter = 0; // BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
					const unsigned int chromaSamplerFilter = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;

					if (needsRecreate) {
						m_vpd = frame._vpd;
						m_videoSize = frame._videoDim;
						if (bgfx::isValid(m_lumaTexture))
							bgfx::destroy(m_lumaTexture);
						if (bgfx::isValid(m_chromaTexture))
							bgfx::destroy(m_chromaTexture);

						switch (m_vpd._PF) {
						case ax::MEVideoPixelFormat::NV12:
							m_lumaTexture = bgfx::createTexture2D(bufferDim.x, bufferDim.y, false, 1, bgfx::TextureFormat::R8, lumaSamplerFilter | BGFX_SAMPLER_W_CLAMP | BGFX_SAMPLER_V_CLAMP);
							m_chromaTexture = bgfx::createTexture2D(bufferDim.x >> 1, bufferDim.y >> 1, false, 1, bgfx::TextureFormat::RG8, chromaSamplerFilter | BGFX_SAMPLER_W_CLAMP | BGFX_SAMPLER_V_CLAMP);
							break;
						case ax::MEVideoPixelFormat::YUY2:
							m_lumaTexture = bgfx::createTexture2D(bufferDim.x, bufferDim.y, false, 1, bgfx::TextureFormat::RG8, lumaSamplerFilter | BGFX_SAMPLER_W_CLAMP | BGFX_SAMPLER_V_CLAMP);
							m_chromaTexture = bgfx::createTexture2D(bufferDim.x >> 1, bufferDim.y, false, 1, bgfx::TextureFormat::RGBA8, chromaSamplerFilter | BGFX_SAMPLER_W_CLAMP | BGFX_SAMPLER_V_CLAMP);
							break;
						case ax::MEVideoPixelFormat::RGB32:
						case ax::MEVideoPixelFormat::BGR32:
							m_lumaTexture = bgfx::createTexture2D(bufferDim.x, bufferDim.y, false, 1, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_W_CLAMP | BGFX_SAMPLER_V_CLAMP);
							break;
						}
					}


					if (m_vpd._PF < ax::MEVideoPixelFormat::RGB32) {
						if (m_vpd._PF == ax::MEVideoPixelFormat::NV12) {
							auto lumaBuffer = new yasio::byte_buffer(frame._dataPointer, frame._dataPointer + bufferDim.x * bufferDim.y, std::true_type{});
							auto chromaBuffer = new yasio::byte_buffer(frame._cbcrDataPointer, frame._cbcrDataPointer + ((bufferDim.x * bufferDim.y) >> 1), std::true_type{});
							const bgfx::Memory* lumaTextureData = bgfx::makeRef(lumaBuffer->data(), bufferDim.x * bufferDim.y, [](void* _ptr, void* _userData) {
								auto lb = reinterpret_cast<yasio::byte_buffer*>(_userData);
								delete lb;
								}, lumaBuffer);

							const bgfx::Memory* chromaTextureData = bgfx::makeRef(chromaBuffer->data(), (bufferDim.x * bufferDim.y) >> 1, [](void* _ptr, void* _userData) {
								auto cb = reinterpret_cast<yasio::byte_buffer*>(_userData);
								delete cb;
								}, chromaBuffer);

							bgfx::updateTexture2D(m_lumaTexture, 0, 0, 0, 0, bufferDim.x, bufferDim.y, lumaTextureData);
							bgfx::updateTexture2D(m_chromaTexture, 0, 0, 0, 0, bufferDim.x >> 1, bufferDim.y >> 1, chromaTextureData);
						}
						else if (m_vpd._PF == ax::MEVideoPixelFormat::YUY2) {
							auto lumaBuffer = new TSRefByteBuffer(frame._dataPointer, frame._dataPointer + frame._dataLen, std::true_type{});

							const bgfx::Memory* lumaTextureData = bgfx::makeRef(lumaBuffer->data(), lumaBuffer->size(), [](void* _ptr, void* _userData) {
								auto lb = reinterpret_cast<TSRefByteBuffer*>(_userData);
								lb->release();
								}, lumaBuffer);

							auto chromaBuffer = lumaBuffer->incRef();
							const bgfx::Memory* chromaTextureData = bgfx::makeRef(chromaBuffer->data(), chromaBuffer->size(), [](void* _ptr, void* _userData) {
								auto cb = reinterpret_cast<TSRefByteBuffer*>(_userData);
								cb->release();
								}, chromaBuffer);

							bgfx::updateTexture2D(m_lumaTexture, 0, 0, 0, 0, bufferDim.x, bufferDim.y, lumaTextureData);
							bgfx::updateTexture2D(m_chromaTexture, 0, 0, 0, 0, bufferDim.x >> 1, bufferDim.y, chromaTextureData);
						}
					}
					else {
						auto lumaBuffer = new yasio::byte_buffer(frame._dataPointer, frame._dataPointer + frame._dataLen, std::true_type{});
						const bgfx::Memory* lumaTextureData = bgfx::makeRef(lumaBuffer->data(), bufferDim.x * bufferDim.y, [](void* _ptr, void* _userData) {
							auto lb = reinterpret_cast<yasio::byte_buffer*>(_userData);
							delete lb;
							}, lumaBuffer);
						bgfx::updateTexture2D(m_lumaTexture, 0, 0, 0, 0, bufferDim.x, bufferDim.y, lumaTextureData);

						bgfx::setTexture(0, m_lumaTextureLoc, m_lumaTexture);
					}

					});
			}

			if (m_videoFrameCount > 0) {

				bgfx::setTexture(0, m_lumaTextureLoc, m_lumaTexture);
				if (m_vpd._PF < ax::MEVideoPixelFormat::RGB32)
					bgfx::setTexture(1, m_chromaTextureLoc, m_chromaTexture);
				updateTextureRect(false, m_vpd._dim, m_videoSize);

				switch (m_vpd._PF) {
				case ax::MEVideoPixelFormat::NV12:
					currProgram = &m_programNV12;
					break;
				case ax::MEVideoPixelFormat::YUY2:
					currProgram = &m_programYUY2;
					break;
				case ax::MEVideoPixelFormat::BGR32:
					currProgram = &m_programBGRA;
				case ax::MEVideoPixelFormat::RGB32:
					currProgram = &m_program;
					break;
				}
			}
			else {
				bgfx::setTexture(0, m_spriteTextureLoc, m_spriteTexture);

				updateTextureRect(false, m_spriteSize, m_spriteSize);
			}

			bgfx::TransientVertexBuffer tvb;
			bgfx::TransientIndexBuffer tib;
			if (bgfx::allocTransientBuffers(&tvb, PosTexColorVertex::ms_decl, 4, &tib, 6)) {
				uint16_t* indices = (uint16_t*)tib.data;
				memcpy(indices, s_triList, sizeof(s_triList));

				bgfx::setIndexBuffer(&tib);

				PosTexColorVertex* vertex = (PosTexColorVertex*)tvb.data;
				memcpy(vertex, s_vertices, sizeof(s_vertices));

				bgfx::setVertexBuffer(0, &tvb);

				if (m_vpd._PF == ax::MEVideoPixelFormat::NV12 || m_vpd._PF == ax::MEVideoPixelFormat::YUY2)
					bgfx::setUniform(m_colorTransformLoc, &s_YuvToRgbRec709Scaled);

				bgfx::setState(state);
				bgfx::submit(0, *currProgram);
			}
		}

		entry::MouseState m_mouseState;

		uint32_t m_width;
		uint32_t m_height;
		uint32_t m_debug;
		uint32_t m_reset;
	};

} // namespace

ENTRY_IMPLEMENT_MAIN(
	ExampleHelloWorld
	, "bgfx-mplay"
	, "Initialization and debug text."
	, "https://github.com/halx99/bgfx-axplay/tree/axplay"
);
