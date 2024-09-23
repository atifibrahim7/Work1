// minimalistic code to draw a single triangle, this is not part of the API.
#include "shaderc/shaderc.h" // needed for compiling shaders at runtime
#ifdef _WIN32 // must use MT platform DLL libraries on windows
#pragma comment(lib, "shaderc_combined.lib") 
#endif

void PrintLabeledDebugString(const char* label, const char* toPrint)
{
	std::cout << label << toPrint << std::endl;

	//OutputDebugStringA is a windows-only function 
#if defined WIN32 
	OutputDebugStringA(label);
	OutputDebugStringA(toPrint);
#endif
}

class Renderer
{
	// proxy handles
	GW::SYSTEM::GWindow win;
	GW::GRAPHICS::GVulkanSurface vlk;
	VkRenderPass renderPass;
	GW::CORE::GEventReceiver shutdown;

	// what we need at a minimum to draw a triangle
	VkDevice device = nullptr;
	VkPhysicalDevice physicalDevice = nullptr;

	VkBuffer unifiedBufferHandle = nullptr;
	VkDeviceMemory unifiedBufferData = nullptr;
	VkShaderModule vertexShader = nullptr;
	VkShaderModule fragmentShader = nullptr;
	VkPipeline pipeline = nullptr;
	VkPipelineLayout pipelineLayout = nullptr;

	unsigned int windowWidth, windowHeight;

	VkBuffer geometryBuffer = nullptr;
	VkDeviceMemory geometryBufferMemory = nullptr;
	VkDeviceSize vertexBufferOffset = 0;
	VkDeviceSize indexBufferOffset = 0;
	uint32_t indexCount = 0;
	VkIndexType indexType = VK_INDEX_TYPE_UINT16;
	tinygltf::Model model; 
	tinygltf::TinyGLTF loader;	
	

	std::vector<VkDeviceSize> attributeOffsets;
	std::vector<VkDeviceSize> attributeSizes;
	VkDeviceSize totalBufferSize;

public:
	Renderer(GW::SYSTEM::GWindow _win, GW::GRAPHICS::GVulkanSurface _vlk)
	{
		win = _win;
		vlk = _vlk;
		LoadGLTFModel("../Models/blender_bebe.gltf");

		UpdateWindowDimensions();
		InitializeGraphics();
		BindShutdownCallback();
	}

private:

	void LoadGLTFModel(const std::string& filepath)
	{
		std::string err;
		std::string warn;

		bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, filepath);

		if (!warn.empty()) {
			std::cout << "GLTF Warning: " << warn << std::endl;
		}

		if (!err.empty()) {
			std::cout << "GLTF Error: " << err << std::endl;
		}

		if (!ret) {
			throw std::runtime_error("Failed to load GLTF model");
		}

		std::cout << "Loaded GLTF model with " << model.meshes.size() << " meshes" << std::endl;

		const tinygltf::Mesh& mesh = model.meshes[0];
		const tinygltf::Primitive& primitive = mesh.primitives[0]; // Assuming a single primitive

		// Get the index accessor and its bufferView
		const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
		const tinygltf::BufferView& indexBufferView = model.bufferViews[indexAccessor.bufferView];
		 indexBufferOffset = indexBufferView.byteOffset;

		// Get the vertex accessor (for POSITION) and its bufferView
		int positionAccessorIndex = primitive.attributes.find("POSITION")->second;
		const tinygltf::Accessor& positionAccessor = model.accessors[positionAccessorIndex];
		const tinygltf::BufferView& vertexBufferView = model.bufferViews[positionAccessor.bufferView];
		 vertexBufferOffset = vertexBufferView.byteOffset;
		  indexCount = indexAccessor.count;

	}




	void UpdateWindowDimensions()
	{
		win.GetClientWidth(windowWidth);
		win.GetClientHeight(windowHeight);
	}

	void InitializeGraphics()
	{
		GetHandlesFromSurface();
		InitializeVertexBuffer();
		//CreateGeometryBuffer();
	
		CompileShaders();
		InitializeGraphicsPipeline();
	}

	void GetHandlesFromSurface()
	{
		vlk.GetDevice((void**)&device);
		vlk.GetPhysicalDevice((void**)&physicalDevice);
		vlk.GetRenderPass((void**)&renderPass);
	}

	void InitializeVertexBuffer()
	{
		float verts[] =
		{
			0,   0.5f,
			0.5f, -0.5f,
			-0.5f, -0.5f
		};

		CreateUnifiedBuffer();
	}

	void CreateUnifiedBuffer()
	{
		const tinygltf::Mesh& mesh = model.meshes[0];
		const tinygltf::Primitive& primitive = mesh.primitives[0];

		attributeOffsets.resize(4);
		attributeSizes.resize(4);
		totalBufferSize = 0;

		// Calculate sizes and offsets for each attribute
		for (int i = 0; i < 4; ++i)
		{
			const tinygltf::Accessor& accessor = model.accessors[i];
			const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
			attributeSizes[i] = bufferView.byteLength;
			attributeOffsets[i] = totalBufferSize;
			totalBufferSize += attributeSizes[i];
		}

		// Add index buffer size
		const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
		const tinygltf::BufferView& indexBufferView = model.bufferViews[indexAccessor.bufferView];
		VkDeviceSize indexBufferSize = indexBufferView.byteLength;
		indexBufferOffset = totalBufferSize;
		totalBufferSize += indexBufferSize;

		// Create unified buffer
		GvkHelper::create_buffer(physicalDevice, device, totalBufferSize,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&unifiedBufferHandle, &unifiedBufferData);

		// Copy vertex data
		for (int i = 0; i < 4; ++i)
		{
			const tinygltf::Accessor& accessor = model.accessors[i];
			const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
			const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

			void* data;
			vkMapMemory(device, unifiedBufferData, attributeOffsets[i], attributeSizes[i], 0, &data);
			memcpy(data, &buffer.data[bufferView.byteOffset], attributeSizes[i]);
			vkUnmapMemory(device, unifiedBufferData);
		}

		// Copy index data
		void* data;
		vkMapMemory(device, unifiedBufferData, indexBufferOffset, indexBufferSize, 0, &data);
		memcpy(data, &model.buffers[indexBufferView.buffer].data[indexBufferView.byteOffset], indexBufferSize);
		vkUnmapMemory(device, unifiedBufferData);

		indexCount = indexAccessor.count;


		std::cout << "Total buffer size: " << totalBufferSize << std::endl;
		for (int i = 0; i < 4; ++i)
		{
			std::cout << "Attribute " << i << " - Offset: " << attributeOffsets[i]
				<< ", Size: " << attributeSizes[i] << std::endl;
		}
		std::cout << "Index buffer - Offset: " << indexBufferOffset
			<< ", Size: " << indexBufferSize << std::endl;
	}
	//void CreateUnifiedBuffer()
	//{
	//	const tinygltf::Mesh& mesh = model.meshes[0];
	//	const tinygltf::Primitive& primitive = mesh.primitives[0];

	//	const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
	//	const tinygltf::BufferView& indexBufferView = model.bufferViews[indexAccessor.bufferView];
	//	const tinygltf::Buffer& indexBuffer = model.buffers[indexBufferView.buffer];

	//	int positionAccessorIndex = primitive.attributes.find("POSITION")->second;
	//	const tinygltf::Accessor& positionAccessor = model.accessors[positionAccessorIndex];
	//	const tinygltf::BufferView& positionBufferView = model.bufferViews[positionAccessor.bufferView];
	//	const tinygltf::Buffer& positionBuffer = model.buffers[positionBufferView.buffer];

	//	VkDeviceSize indexBufferSize = indexAccessor.count * sizeof(uint16_t);
	//	VkDeviceSize vertexBufferSize = positionAccessor.count * 3 * sizeof(float);

	//	// Ensure index buffer size is aligned to 4 bytes
	//	VkDeviceSize alignedIndexBufferSize = (indexBufferSize + 3) & ~3;

	//	VkDeviceSize totalBufferSize = alignedIndexBufferSize + vertexBufferSize;

	//	// Create unified buffer
	//	GvkHelper::create_buffer(physicalDevice, device, totalBufferSize,
	//		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
	//		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
	//		&unifiedBufferHandle, &unifiedBufferData);

	//	// Copy index data
	//	void* data;
	//	vkMapMemory(device, unifiedBufferData, 0, alignedIndexBufferSize, 0, &data);
	//	memcpy(data, &indexBuffer.data[indexBufferView.byteOffset], indexBufferSize);
	//	vkUnmapMemory(device, unifiedBufferData);

	//	// Copy vertex data
	//	vkMapMemory(device, unifiedBufferData, alignedIndexBufferSize, vertexBufferSize, 0, &data);
	//	memcpy(data, &positionBuffer.data[positionBufferView.byteOffset], vertexBufferSize);
	//	vkUnmapMemory(device, unifiedBufferData);

	//	indexBufferOffset = 0;
	//	vertexBufferOffset = alignedIndexBufferSize;
	//	indexCount = indexAccessor.count;
	//}

	void CompileShaders()
	{
		// Intialize runtime shader compiler HLSL -> SPIRV
		shaderc_compiler_t compiler = shaderc_compiler_initialize();
		shaderc_compile_options_t options = CreateCompileOptions();

		CompileVertexShader(compiler, options);
		CompileFragmentShader(compiler, options);

		// Free runtime shader compiler resources
		shaderc_compile_options_release(options);
		shaderc_compiler_release(compiler);
	}

	shaderc_compile_options_t CreateCompileOptions()
	{
		shaderc_compile_options_t retval = shaderc_compile_options_initialize();
		shaderc_compile_options_set_source_language(retval, shaderc_source_language_hlsl);
		shaderc_compile_options_set_invert_y(retval, true);
#ifndef NDEBUG
		shaderc_compile_options_set_generate_debug_info(retval);
#endif
		return retval;
	}

	void CompileVertexShader(const shaderc_compiler_t& compiler, const shaderc_compile_options_t& options)
	{
		std::string vertexShaderSource = ReadFileIntoString("../VertexShader.hlsl");

		shaderc_compilation_result_t result = shaderc_compile_into_spv( // compile
			compiler, vertexShaderSource.c_str(), vertexShaderSource.length(),
			shaderc_vertex_shader, "main.vert", "main", options);

		if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) // errors?
		{
			PrintLabeledDebugString("Vertex Shader Errors: \n", shaderc_result_get_error_message(result));
			abort(); //Vertex shader failed to compile! 
			return;
		}

		GvkHelper::create_shader_module(device, shaderc_result_get_length(result), // load into Vulkan
			(char*)shaderc_result_get_bytes(result), &vertexShader);

		shaderc_result_release(result); // done
	}

	void CompileFragmentShader(const shaderc_compiler_t& compiler, const shaderc_compile_options_t& options)
	{
		std::string fragmentShaderSource = ReadFileIntoString("../FragmentShader.hlsl");

		shaderc_compilation_result_t result = shaderc_compile_into_spv( // compile
			compiler, fragmentShaderSource.c_str(), fragmentShaderSource.length(),
			shaderc_fragment_shader, "main.frag", "main", options);

		if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) // errors?
		{
			PrintLabeledDebugString("Fragment Shader Errors: \n", shaderc_result_get_error_message(result));
			abort(); //Fragment shader failed to compile! 
			return;
		}

		GvkHelper::create_shader_module(device, shaderc_result_get_length(result), // load into Vulkan
			(char*)shaderc_result_get_bytes(result), &fragmentShader);

		shaderc_result_release(result); // done
	}
	
	std::vector<VkVertexInputAttributeDescription> CreateVkVertexInputAttributeDescriptions()
{
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions(4);

    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = 0;

    attributeDescriptions[1].binding = 1;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = 0;

    attributeDescriptions[2].binding = 2;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = 0;

    attributeDescriptions[3].binding = 3;
    attributeDescriptions[3].location = 3;
    attributeDescriptions[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions[3].offset = 0;

    return attributeDescriptions;
}

	std::vector<VkVertexInputBindingDescription> CreateVkVertexInputBindingDescriptions() 
	{
		std::vector<VkVertexInputBindingDescription> bindingDescriptions(4);

		for (int i = 0; i < 4; ++i)
		{
			bindingDescriptions[i].binding = i;
			bindingDescriptions[i].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		}

		// Set strides based on the accessor's byte stride
		bindingDescriptions[0].stride = model.accessors[0].ByteStride(model.bufferViews[model.accessors[0].bufferView]);
		bindingDescriptions[1].stride = model.accessors[1].ByteStride(model.bufferViews[model.accessors[1].bufferView]);
		bindingDescriptions[2].stride = model.accessors[2].ByteStride(model.bufferViews[model.accessors[2].bufferView]);
		bindingDescriptions[3].stride = model.accessors[3].ByteStride(model.bufferViews[model.accessors[3].bufferView]);

		return bindingDescriptions;
	}
	// Create Pipeline & Layout (Thanks Tiny!)
	void InitializeGraphicsPipeline()
	{
		VkPipelineShaderStageCreateInfo stage_create_info[2] = {};

		// Create Stage Info for Vertex Shader
		stage_create_info[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage_create_info[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stage_create_info[0].module = vertexShader;
		stage_create_info[0].pName = "main";

		// Create Stage Info for Fragment Shader
		stage_create_info[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage_create_info[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stage_create_info[1].module = fragmentShader;
		stage_create_info[1].pName = "main";


		VkPipelineInputAssemblyStateCreateInfo assembly_create_info = CreateVkPipelineInputAssemblyStateCreateInfo();
		auto vertex_binding_description = CreateVkVertexInputBindingDescriptions();


		//vertex_binding_description.binding = 0;
		//vertex_binding_description.stride = 3 * sizeof(float); // 3 floats for position (12 bytes)
		//vertex_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		auto attributeDescriptions = CreateVkVertexInputAttributeDescriptions();


		/*VkVertexInputAttributeDescription vertex_attribute_descriptions[1];
		vertex_attribute_descriptions[0].binding = 0;
		vertex_attribute_descriptions[0].location = 0;
		vertex_attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		vertex_attribute_descriptions[0].offset = 0;*/
		VkPipelineVertexInputStateCreateInfo input_vertex_info = CreateVkPipelineVertexInputStateCreateInfo(
			vertex_binding_description.data(), vertex_binding_description.size(),
			attributeDescriptions.data(), attributeDescriptions.size());
		VkViewport viewport = CreateViewportFromWindowDimensions();
		VkRect2D scissor = CreateScissorFromWindowDimensions();
		VkPipelineViewportStateCreateInfo viewport_create_info = CreateVkPipelineViewportStateCreateInfo(&viewport, 1, &scissor, 1);
		VkPipelineRasterizationStateCreateInfo rasterization_create_info = CreateVkPipelineRasterizationStateCreateInfo();
		VkPipelineMultisampleStateCreateInfo multisample_create_info = CreateVkPipelineMultisampleStateCreateInfo();
		VkPipelineDepthStencilStateCreateInfo depth_stencil_create_info = CreateVkPipelineDepthStencilStateCreateInfo();
		VkPipelineColorBlendAttachmentState color_blend_attachment_state = CreateVkPipelineColorBlendAttachmentState();
		VkPipelineColorBlendStateCreateInfo color_blend_create_info = CreateVkPipelineColorBlendStateCreateInfo(&color_blend_attachment_state, 1);

		VkDynamicState dynamic_states[2] =
		{
			// By setting these we do not need to re-create the pipeline on Resize
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};

		VkPipelineDynamicStateCreateInfo dynamic_create_info = CreateVkPipelineDynamicStateCreateInfo(dynamic_states, 2);



		CreatePipelineLayout();

		// Pipeline State... (FINALLY) 
		VkGraphicsPipelineCreateInfo pipeline_create_info = {};
		pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeline_create_info.stageCount = 2;
		pipeline_create_info.pStages = stage_create_info;
		pipeline_create_info.pInputAssemblyState = &assembly_create_info;
		pipeline_create_info.pVertexInputState = &input_vertex_info;
		pipeline_create_info.pViewportState = &viewport_create_info;
		pipeline_create_info.pRasterizationState = &rasterization_create_info;
		pipeline_create_info.pMultisampleState = &multisample_create_info;
		pipeline_create_info.pDepthStencilState = &depth_stencil_create_info;
		pipeline_create_info.pColorBlendState = &color_blend_create_info;
		pipeline_create_info.pDynamicState = &dynamic_create_info;
		pipeline_create_info.layout = pipelineLayout;
		pipeline_create_info.renderPass = renderPass;
		pipeline_create_info.subpass = 0;
		pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;

		vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &pipeline);
	}

	VkPipelineShaderStageCreateInfo CreateVertexShaderStageCreateInfo()
	{
		VkPipelineShaderStageCreateInfo retval;
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		retval.stage = VK_SHADER_STAGE_VERTEX_BIT;
		retval.module = vertexShader;
		retval.pName = "main";
		return retval;
	}

	VkPipelineInputAssemblyStateCreateInfo CreateVkPipelineInputAssemblyStateCreateInfo()
	{
		VkPipelineInputAssemblyStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		retval.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		retval.primitiveRestartEnable = false;
		return retval;
	}

	
	VkPipelineVertexInputStateCreateInfo CreateVkPipelineVertexInputStateCreateInfo(
		VkVertexInputBindingDescription* inputBindingDescriptions, unsigned int bindingCount,
		VkVertexInputAttributeDescription* vertexAttributeDescriptions, unsigned int attributeCount)
	{
		VkPipelineVertexInputStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		retval.vertexBindingDescriptionCount = bindingCount;
		retval.pVertexBindingDescriptions = inputBindingDescriptions;
		retval.vertexAttributeDescriptionCount = attributeCount;
		retval.pVertexAttributeDescriptions = vertexAttributeDescriptions;
		return retval;
	}

	VkViewport CreateViewportFromWindowDimensions()
	{
		VkViewport retval = {};
		retval.x = 0;
		retval.y = 0;
		retval.width = static_cast<float>(windowWidth);
		retval.height = static_cast<float>(windowHeight);
		retval.minDepth = 0;
		retval.maxDepth = 1;
		return retval;
	}

	VkRect2D CreateScissorFromWindowDimensions()
	{
		VkRect2D retval = {};
		retval.offset.x = 0;
		retval.offset.y = 0;
		retval.extent.width = windowWidth;
		retval.extent.height = windowHeight;
		return retval;
	}

	VkPipelineViewportStateCreateInfo CreateVkPipelineViewportStateCreateInfo(VkViewport* viewports, unsigned int viewportCount, VkRect2D* scissors, unsigned int scissorCount)
	{
		VkPipelineViewportStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		retval.viewportCount = viewportCount;
		retval.pViewports = viewports;
		retval.scissorCount = scissorCount;
		retval.pScissors = scissors;
		return retval;
	}

	VkPipelineRasterizationStateCreateInfo CreateVkPipelineRasterizationStateCreateInfo()
	{
		VkPipelineRasterizationStateCreateInfo rasterizer = {};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;  // Enable back-face culling
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;  // Set to counter-clockwise
		rasterizer.depthBiasEnable = VK_FALSE;
		rasterizer.depthBiasConstantFactor = 0.0f;
		rasterizer.depthBiasClamp = 0.0f;
		rasterizer.depthBiasSlopeFactor = 0.0f;
		return rasterizer;
	}

	VkPipelineMultisampleStateCreateInfo CreateVkPipelineMultisampleStateCreateInfo()
	{
		VkPipelineMultisampleStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		retval.sampleShadingEnable = VK_FALSE;
		retval.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		retval.minSampleShading = 1.0f;
		retval.pSampleMask = VK_NULL_HANDLE;
		retval.alphaToCoverageEnable = VK_FALSE;
		retval.alphaToOneEnable = VK_FALSE;
		return retval;
	}

	VkPipelineDepthStencilStateCreateInfo CreateVkPipelineDepthStencilStateCreateInfo()
	{
		VkPipelineDepthStencilStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		retval.depthTestEnable = VK_TRUE;
		retval.depthWriteEnable = VK_TRUE;
		retval.depthCompareOp = VK_COMPARE_OP_LESS;
		retval.depthBoundsTestEnable = VK_FALSE;
		retval.minDepthBounds = 0.0f;
		retval.maxDepthBounds = 1.0f;
		retval.stencilTestEnable = VK_FALSE;
		return retval;
	}

	VkPipelineColorBlendAttachmentState CreateVkPipelineColorBlendAttachmentState()
	{
		VkPipelineColorBlendAttachmentState retval = {};
		retval.colorWriteMask = 0xF;
		retval.blendEnable = VK_FALSE;
		retval.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
		retval.dstColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
		retval.colorBlendOp = VK_BLEND_OP_ADD;
		retval.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		retval.dstAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
		retval.alphaBlendOp = VK_BLEND_OP_ADD;
		return retval;
	}

	VkPipelineColorBlendStateCreateInfo CreateVkPipelineColorBlendStateCreateInfo(VkPipelineColorBlendAttachmentState* attachments, unsigned int attachmentCount)
	{
		VkPipelineColorBlendStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		retval.logicOpEnable = VK_FALSE;
		retval.logicOp = VK_LOGIC_OP_COPY;
		retval.attachmentCount = attachmentCount;
		retval.pAttachments = attachments;
		retval.blendConstants[0] = 0.0f;
		retval.blendConstants[1] = 0.0f;
		retval.blendConstants[2] = 0.0f;
		retval.blendConstants[3] = 0.0f;
		return retval;
	}

	VkPipelineDynamicStateCreateInfo CreateVkPipelineDynamicStateCreateInfo(VkDynamicState* dynamicStates, unsigned int dynamicStateCount)
	{
		VkPipelineDynamicStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		retval.dynamicStateCount = dynamicStateCount;
		retval.pDynamicStates = dynamicStates;
		return retval;
	}

	void CreatePipelineLayout()
	{

		VkPipelineLayoutCreateInfo pipeline_layout_create_info = {};
		pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_create_info.setLayoutCount = 0;
		pipeline_layout_create_info.pSetLayouts = VK_NULL_HANDLE; 
		pipeline_layout_create_info.pushConstantRangeCount = 0;
		pipeline_layout_create_info.pPushConstantRanges = nullptr;

		vkCreatePipelineLayout(device, &pipeline_layout_create_info, nullptr, &pipelineLayout);
	}

	void BindShutdownCallback()
	{
		// GVulkanSurface will inform us when to release any allocated resources
		shutdown.Create(vlk, [&]() {
			if (+shutdown.Find(GW::GRAPHICS::GVulkanSurface::Events::RELEASE_RESOURCES, true)) {
				CleanUp(); // unlike D3D we must be careful about destroy timing
			}
			});
	}


public:
	void Render()
	{
		VkCommandBuffer commandBuffer = GetCurrentCommandBuffer();
		SetUpPipeline(commandBuffer);


	

		VkDeviceSize offsets[] = { vertexBufferOffset };
		//VkDeviceSize vertexBufferOffset = 8; // Vertex data starts at byte offset 8 in the unified buffer
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &unifiedBufferHandle, offsets);

		// Bind the unified buffer as index buffer
		//VkDeviceSize indexBufferOffset = 0; // Index data starts at byte offset 0 in the unified buffer
		vkCmdBindIndexBuffer(commandBuffer, unifiedBufferHandle, indexBufferOffset, VK_INDEX_TYPE_UINT16);

		// Use vkCmdDrawIndexed to draw the mesh
		 
		//std::cout << indexCount << "    " << indexBufferOffset <<	vertexBufferOffset	<<  std::endl;


		//uint32_t indexCount = 3; // Number of indices from GLTF accessor
		vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
		//vkCmdDraw(commandBuffer, 3, 1, 0, 0); 
	}


private:
	VkCommandBuffer GetCurrentCommandBuffer()
	{
		unsigned int currentBuffer;
		vlk.GetSwapchainCurrentImage(currentBuffer);

		VkCommandBuffer commandBuffer;
		vlk.GetCommandBuffer(currentBuffer, (void**)&commandBuffer);
		return commandBuffer;
	}

	void SetUpPipeline(VkCommandBuffer& commandBuffer)
	{
		UpdateWindowDimensions();
		SetViewport(commandBuffer);
		SetScissor(commandBuffer);
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		BindVertexBuffers(commandBuffer);
	}

	void SetViewport(const VkCommandBuffer& commandBuffer)
	{
		VkViewport viewport = CreateViewportFromWindowDimensions();
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
	}

	void SetScissor(const VkCommandBuffer& commandBuffer)
	{
		VkRect2D scissor = CreateScissorFromWindowDimensions();
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
	}

	/*void BindVertexBuffers(VkCommandBuffer& commandBuffer)
	{
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &unifiedBufferHandle, offsets);
	}*/

	void BindVertexBuffers(VkCommandBuffer& commandBuffer)
	{
		std::vector<VkBuffer> buffers(4, unifiedBufferHandle);
		vkCmdBindVertexBuffers(commandBuffer, 0, 4, buffers.data(), attributeOffsets.data());
	}

	//Cleanup callback function (passed to VKSurface, will be called when the pipeline shuts down)
	void CleanUp()
	{
		// wait till everything has completed
		vkDeviceWaitIdle(device);

		// Release allocated buffers, shaders & pipeline
		vkDestroyBuffer(device, unifiedBufferHandle, nullptr);
		vkFreeMemory(device, unifiedBufferData, nullptr);
		vkDestroyShaderModule(device, vertexShader, nullptr);
		vkDestroyShaderModule(device, fragmentShader, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyPipeline(device, pipeline, nullptr);
	}
};
