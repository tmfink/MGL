/*
 * Copyright (C) Michael Larson on 1/6/2022
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * program.c
 * MGL
 *
 */

#include <stdio.h>
#include <string.h>
#include <glslang_c_interface.h>
#include <glslang_c_shader_types.h>
#include "spirv-tools/libspirv.h"
#include "spirv_cross_c.h"
#include "spirv.h"

#include "shaders.h"
#include "glm_context.h"

Program *newProgram(GLMContext ctx, GLuint program)
{
    Program *ptr;

    ptr = (Program *)malloc(sizeof(Program));
    assert(ptr);

    bzero(ptr, sizeof(Program));

    ptr->name = program;

    return ptr;
}

Program *getProgram(GLMContext ctx, GLuint program)
{
    Program *ptr;

    ptr = (Program *)searchHashTable(&STATE(program_table), program);

    if (!ptr)
    {
        ptr = newProgram(ctx, program);

        insertHashElement(&STATE(program_table), program, ptr);
    }

    return ptr;
}

int isProgram(GLMContext ctx, GLuint program)
{
    Program *ptr;

    ptr = (Program *)searchHashTable(&STATE(program_table), program);

    if (ptr)
        return 1;

    return 0;
}

Program *findProgram(GLMContext ctx, GLuint program)
{
    Program *ptr;

    ptr = (Program *)searchHashTable(&STATE(program_table), program);

    return ptr;
}

GLuint mglCreateProgram(GLMContext ctx)
{
    GLuint program;

    program = getNewName(&STATE(program_table));

    getProgram(ctx, program);

    return program;
}

void mglDeleteProgram(GLMContext ctx, GLuint program)
{
    Program *ptr;

    ptr = findProgram(ctx, program);

    if (!ptr)
    {
        assert(0);

        return;
    }

    deleteHashElement(&STATE(program_table), program);

    if (ptr->linked_glsl_program)
    {
        glslang_program_delete(ptr->linked_glsl_program);
    }

    if (ptr->mtl_data)
    {
        assert(0);
    }

    // ptr->spirv_program and such
    assert(0);

    free(ptr);
}

GLboolean mglIsProgram(GLMContext ctx, GLuint program)
{
    if (isProgram(ctx, program))
        return GL_TRUE;

    return GL_FALSE;
}

void mglAttachShader(GLMContext ctx, GLuint program, GLuint shader)
{
    Program *pptr;
    Shader *sptr;
    GLuint index;

    sptr = findShader(ctx, shader);

    if (!sptr)
    {
        assert(0);

        return;
    }

    pptr = findProgram(ctx, program);

    if (!pptr)
    {
        assert(0);

        return;
    }

    index = sptr->glm_type;

    pptr->shader_slots[index] = sptr;
    pptr->dirty_bits |= DIRTY_PROGRAM;
}

void mglDetachShader(GLMContext ctx, GLuint program, GLuint shader)
{
    Program *pptr;
    Shader *sptr;
    GLuint index;

    sptr = findShader(ctx, shader);

    if (!sptr)
    {
        assert(0);

        return;
    }

    pptr = findProgram(ctx, program);

    if (!pptr)
    {
        assert(0);

        return;
    }

    index = sptr->glm_type;

    pptr->shader_slots[index] = NULL;
    pptr->dirty_bits |= DIRTY_PROGRAM;

    assert(0); // need to do something with metal at this point
}

void error_callback(void *userdata, const char *error)
{
    GLMContext err_ctx;

    err_ctx = (GLMContext)userdata;
    assert(error);
    printf("parseSPIRVShader error:%s\n", error);
}


static_assert(_VERTEX_SHADER == GLSLANG_STAGE_VERTEX, "_VERTEX_SHADER == GLSLANG_STAGE_VERTEX failed");
static_assert(_TESS_CONTROL_SHADER == GLSLANG_STAGE_TESSCONTROL, "_TESS_CONTROL_SHADER == GLSLANG_STAGE_TESSCONTROL failed");
static_assert(_TESS_EVALUATION_SHADER == GLSLANG_STAGE_TESSEVALUATION, "_TESS_EVALUATION_SHADER == GLSLANG_STAGE_TESSEVALUATION failed");
static_assert(_GEOMETRY_SHADER == GLSLANG_STAGE_GEOMETRY, "_GEOMETRY_SHADER == GLSLANG_STAGE_GEOMETRY failed");
static_assert(_FRAGMENT_SHADER == GLSLANG_STAGE_FRAGMENT, "_FRAGMENT_SHADER == GLSLANG_STAGE_FRAGMENT failed");
static_assert(_COMPUTE_SHADER == GLSLANG_STAGE_COMPUTE, "_COMPUTE_SHADER == GLSLANG_STAGE_COMPUTE failed");

void addShadersToProgram(GLMContext ctx, Program *pptr, glslang_program_t *glsl_program)
{
    // add shaders
    for(int i=0;i<_MAX_SHADER_TYPES; i++)
    {
        Shader *ptr;

        ptr = pptr->shader_slots[i];

        if(ptr)
        {
            // should have glsl shader here
            assert(ptr->compiled_glsl_shader);

            glslang_program_add_shader(glsl_program, ptr->compiled_glsl_shader);
        }
    }
}

char *parseSPIRVShaderToMetal(GLMContext ctx, Program *ptr, int stage)
{
    const SpvId *spirv;
    size_t word_count;
    char *str_ret;
    int parse_res;

    spvc_context context = NULL;
    spvc_parsed_ir ir = NULL;
    spvc_compiler compiler_msl = NULL;
    spvc_compiler_options options = NULL;
    spvc_resources resources = NULL;
    const spvc_reflected_resource *list = NULL;
    const char *result = NULL;
    size_t count;
    size_t i;

    spirv = ptr->spirv[stage].ir;
    assert(spirv);
    word_count = ptr->spirv[stage].size;
    assert(spirv);

    // Create context.
    spvc_context_create(&context);
    assert(context);

    // Set debug callback.
    spvc_context_set_error_callback(context, error_callback, ctx);

    // Parse the SPIR-V.
    parse_res = spvc_context_parse_spirv(context, spirv, word_count, &ir);
    assert(parse_res == SPVC_SUCCESS);

    // Hand it off to a compiler instance and give it ownership of the IR.
    spvc_context_create_compiler(context, SPVC_BACKEND_MSL, ir, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &compiler_msl);
    assert(compiler_msl);

    // create an entry point for metal based on the shader type and name
    GLuint name;
    char entry_point[128];
    name = ptr->shader_slots[stage]->name;

    SpvExecutionModel model;
    switch(stage)
    {
        case _VERTEX_SHADER: model = SpvExecutionModelVertex; break;
        case _TESS_CONTROL_SHADER: model = SpvExecutionModelTessellationControl; break;
        case _TESS_EVALUATION_SHADER: model = SpvExecutionModelTessellationEvaluation; break;
        case _GEOMETRY_SHADER: model = SpvExecutionModelGeometry; break;
        case _FRAGMENT_SHADER: model = SpvExecutionModelFragment; break;
        case _COMPUTE_SHADER: model = SpvExecutionModelGLCompute; break;
        default: assert(0);
    }

    switch(stage)
    {
        case _VERTEX_SHADER: sprintf(entry_point, "vertex_%d_main",name); break;
        case _TESS_CONTROL_SHADER: sprintf(entry_point, "tess_control_%d_main",name); break;
        case _TESS_EVALUATION_SHADER: sprintf(entry_point, "tess_evaluation_%d_main",name); break;
        case _GEOMETRY_SHADER: sprintf(entry_point, "geometry_%d",name); break;
        case _FRAGMENT_SHADER: sprintf(entry_point, "fragment_%d",name); break;
        case _COMPUTE_SHADER: sprintf(entry_point, "compute_%d",name); break;
        default: assert(0);
    }

    const char *cleansed_entry_point;
    cleansed_entry_point = spvc_compiler_get_cleansed_entry_point_name(compiler_msl, "main", model);

    spvc_result err;
    err = spvc_compiler_rename_entry_point(compiler_msl, cleansed_entry_point, entry_point, model);
    assert(err == SPVC_SUCCESS);

    // set the entry point for metal
    ptr->shader_slots[stage]->entry_point = strdup(entry_point);

    // compute shader
    if (stage == _COMPUTE_SHADER)
    {
        spvc_result res;
        const spvc_entry_point *entry_points;
        size_t num_entry_points;

        res = spvc_compiler_get_entry_points(compiler_msl, &entry_points, &num_entry_points);

        for(int i=0; i<num_entry_points; i++)
        {
            printf("Entry point: %s Execution Model: %d\n", entry_points[i].name, entry_points[i].execution_model);
        }

        ptr->local_workgroup_size.x = spvc_compiler_get_execution_mode_argument_by_index(compiler_msl, SpvExecutionModeLocalSize, 0);
        ptr->local_workgroup_size.y = spvc_compiler_get_execution_mode_argument_by_index(compiler_msl, SpvExecutionModeLocalSize, 1);
        ptr->local_workgroup_size.z = spvc_compiler_get_execution_mode_argument_by_index(compiler_msl, SpvExecutionModeLocalSize, 2);
    }
    
    // Do some basic reflection.
    spvc_compiler_create_shader_resources(compiler_msl, &resources);
    for (int res_type=SPVC_RESOURCE_TYPE_UNIFORM_BUFFER; res_type < SPVC_RESOURCE_TYPE_ACCELERATION_STRUCTURE; res_type++)
    {
        const char *res_name[] = {"NONE", "UNIFORM_BUFFER", "STORAGE_BUFFER", "STAGE_INPUT", "STAGE_OUTPUT",
            "SUBPASS_INPUT", "STORAGE_INPUT", "SAMPLED_IMAGE", "ATOMIC_COUNTER", "PUSH_CONSTANT", "SEPARATE_IMAGE",
            "SEPARATE_SAMPLERS", "ACCELERATION_STRUCTURE", "RAY_QUERY"};

        spvc_resources_get_resource_list_for_type(resources, res_type, &list, &count);

        ptr->spirv_resources_list[stage][res_type].count = (GLuint)count;
        ptr->spirv_resources_list[stage][res_type].list = (SpirvResource *)malloc(count * sizeof(SpirvResource));

        for (i = 0; i < count; i++)
        {
            printf("res_type: %s ID: %u, BaseTypeID: %u, TypeID: %u, Name: %s ", res_name[res_type], list[i].id, list[i].base_type_id, list[i].type_id,
                   list[i].name);
            printf("Set: %u, Binding: %u Location: %d Index: %d, Uniform: %d, UniformId: %d\n",
                   spvc_compiler_get_decoration(compiler_msl, list[i].id, SpvDecorationDescriptorSet),
                   spvc_compiler_get_decoration(compiler_msl, list[i].id, SpvDecorationBinding),
                   spvc_compiler_get_decoration(compiler_msl, list[i].id, SpvDecorationLocation),
                   spvc_compiler_get_decoration(compiler_msl, list[i].id, SpvDecorationIndex),
                   spvc_compiler_get_decoration(compiler_msl, list[i].id, SpvDecorationUniform),
                   spvc_compiler_get_decoration(compiler_msl, list[i].id, SpvDecorationUniformId));

            ptr->spirv_resources_list[stage][res_type].list[i]._id = list[i].id;
            ptr->spirv_resources_list[stage][res_type].list[i].base_type_id = list[i].base_type_id;
            ptr->spirv_resources_list[stage][res_type].list[i].type_id = list[i].type_id;
            ptr->spirv_resources_list[stage][res_type].list[i].name = strdup(list[i].name);
            ptr->spirv_resources_list[stage][res_type].list[i].set = spvc_compiler_get_decoration(compiler_msl, list[i].id, SpvDecorationDescriptorSet);
            ptr->spirv_resources_list[stage][res_type].list[i].binding = spvc_compiler_get_decoration(compiler_msl, list[i].id, SpvDecorationBinding);
            ptr->spirv_resources_list[stage][res_type].list[i].location = spvc_compiler_get_decoration(compiler_msl, list[i].id, SpvDecorationLocation);
        }
    }

    // Modify options.
    spvc_compiler_create_compiler_options(compiler_msl, &options);
    spvc_compiler_options_set_uint(options, SPVC_COMPILER_OPTION_MSL_VERSION, SPVC_MAKE_MSL_VERSION(2,3,0));
    spvc_compiler_install_compiler_options(compiler_msl, options);

    spvc_compiler_compile(compiler_msl, &result);
    printf("\n%s\n", result);

    str_ret = strdup(result);

    // Frees all memory we allocated so far.
    spvc_context_destroy(context);

    return str_ret;
}

GLuint linkAndCompileProgramToMetal(GLMContext ctx, Program *pptr, int stage)
{
    glslang_program_t *glsl_program;
    int err;

    glsl_program = glslang_program_create();
    assert(glsl_program);

    // shaders to glsl program
    addShadersToProgram(ctx, pptr, glsl_program);

    // link
    err = glslang_program_link(glsl_program, GLSLANG_MSG_DEFAULT_BIT);
    if (!err)
    {
        // this is useful.. but information after this failure isn't that interesting
        printf("glslang_program_link failed err: %d\n", err);
        printf("glslang_program_SPIRV_get_messages:\n%s\n", glslang_program_SPIRV_get_messages(glsl_program));
        printf("glslang_program_get_info_log:\n%s\n", glslang_program_get_info_log(glsl_program));
        printf("glslang_program_get_info_debug_log:\n%s\n", glslang_program_get_info_debug_log(glsl_program));

        assert(err == 0);

        return 1;
    }

    // generate SPIVR
    glslang_program_SPIRV_generate(glsl_program, stage);

    if (glslang_program_SPIRV_get_messages(glsl_program))
    {
        printf("%s\n", glslang_program_SPIRV_get_messages(glsl_program));

        assert(0);
    }

    // save SPIRV code
    pptr->spirv[stage].size = glslang_program_SPIRV_get_size(glsl_program);
    pptr->spirv[stage].ir = (unsigned int *)malloc(pptr->spirv[stage].size * sizeof(unsigned));
    assert(pptr->spirv[stage].ir);
    glslang_program_SPIRV_get(glsl_program, pptr->spirv[stage].ir);

    // compile SPIRV to Metal
    pptr->spirv[stage].msl_str = parseSPIRVShaderToMetal(ctx, pptr, stage);

    pptr->linked_glsl_program = glsl_program;
    pptr->dirty_bits |= DIRTY_PROGRAM;

    free(glsl_program);

    return err;
}

void mglLinkProgram(GLMContext ctx, GLuint program)
{
    Program *pptr;

    pptr = findProgram(ctx, program);

    if (!pptr)
    {
        assert(0);

        return;
    }

    for (int stage=0; stage<_MAX_SHADER_TYPES; stage++)
    {
        if (pptr->shader_slots[stage])
        {
            linkAndCompileProgramToMetal(ctx, pptr, stage);
        }
    }
}

void mglUseProgram(GLMContext ctx, GLuint program)
{
    Program *pptr;

    if (program)
    {
        pptr = findProgram(ctx, program);

        if (!pptr)
        {
            assert(0);

            return;
        }

        if (pptr->linked_glsl_program == NULL)
        {
            assert(0);

            return;
        }
    }
    else
    {
        pptr = NULL;
    }

    ctx->state.program = pptr;
    ctx->state.dirty_bits |= DIRTY_PROGRAM;
}

void mglBindAttribLocation(GLMContext ctx, GLuint program, GLuint index, const GLchar *name)
{
    // Unimplemented function
    assert(0);
}

void mglGetActiveAttrib(GLMContext ctx, GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name)
{
    // Unimplemented function
    assert(0);
}

void mglGetActiveUniform(GLMContext ctx, GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name)
{
    // Unimplemented function
    assert(0);
}

void mglGetAttachedShaders(GLMContext ctx, GLuint program, GLsizei maxCount, GLsizei *count, GLuint *shaders)
{
    // Unimplemented function
    assert(0);
}

GLint  mglGetAttribLocation(GLMContext ctx, GLuint program, const GLchar *name)
{
    GLint ret = -1;

    // Unimplemented function
    assert(0);
    return ret;
}

void mglGetProgramiv(GLMContext ctx, GLuint program, GLenum pname, GLint *params)
{
    // Unimplemented function
    assert(0);
}

void mglGetProgramInfoLog(GLMContext ctx, GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog)
{
    // Unimplemented function
    assert(0);
}

GLint  mglGetUniformLocation(GLMContext ctx, GLuint program, const GLchar *name)
{
    GLint ret = -1;

    // Unimplemented function
    assert(0);
    return ret;
}

void mglGetUniformfv(GLMContext ctx, GLuint program, GLint location, GLfloat *params)
{
    // Unimplemented function
    assert(0);
}

void mglGetUniformiv(GLMContext ctx, GLuint program, GLint location, GLint *params)
{
    // Unimplemented function
    assert(0);
}


void mglGetUniformIndices(GLMContext ctx, GLuint program, GLsizei uniformCount, const GLchar *const*uniformNames, GLuint *uniformIndices)
{
    // Unimplemented function
    assert(0);
}

void mglGetActiveUniformsiv(GLMContext ctx, GLuint program, GLsizei uniformCount, const GLuint *uniformIndices, GLenum pname, GLint *params)
{
    // Unimplemented function
    assert(0);
}

void mglGetActiveUniformName(GLMContext ctx, GLuint program, GLuint uniformIndex, GLsizei bufSize, GLsizei *length, GLchar *uniformName)
{
    // Unimplemented function
    assert(0);
}

GLuint  mglGetUniformBlockIndex(GLMContext ctx, GLuint program, const GLchar *uniformBlockName)
{
    if (isProgram(ctx, program) == GL_FALSE)
    {
        assert(0);

        return 0;
    }

    Program *ptr;

    ptr = getProgram(ctx, program);
    assert(program);

    if (ptr->linked_glsl_program == NULL)
    {
        assert(0);

        return 0;
    }

    for (int stage=_VERTEX_SHADER; stage<_MAX_SHADER_TYPES; stage++)
    {
        int count;

        count = ptr->spirv_resources_list[stage][SPVC_RESOURCE_TYPE_UNIFORM_BUFFER].count;

        for (int i=0; i<count; i++)
        {
            const char *str = ptr->spirv_resources_list[stage][SPVC_RESOURCE_TYPE_UNIFORM_BUFFER].list[i].name;

            if (!strcmp(str, uniformBlockName))
            {
                GLuint binding;

                binding = ptr->spirv_resources_list[stage][SPVC_RESOURCE_TYPE_UNIFORM_BUFFER].list[i].binding;

                return binding;
            }
        }
    }

    assert(0);

    return 0xFFFFFFFF;
}

void mglGetActiveUniformBlockiv(GLMContext ctx, GLuint program, GLuint uniformBlockIndex, GLenum pname, GLint *params)
{
    // Unimplemented function
    assert(0);
}

void mglGetActiveUniformBlockName(GLMContext ctx, GLuint program, GLuint uniformBlockIndex, GLsizei bufSize, GLsizei *length, GLchar *uniformBlockName)
{
    // Unimplemented function
    assert(0);
}

void mglUniformBlockBinding(GLMContext ctx, GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding)
{
    // Unimplemented function
    assert(0);
}


#pragma mark program pipelines
void mglGenProgramPipelines(GLMContext ctx, GLsizei n, GLuint *pipelines)
{
        assert(0);
}

GLboolean mglIsProgramPipeline(GLMContext ctx, GLuint pipeline)
{
        assert(0);
}

void mglDeleteProgramPipelines(GLMContext ctx, GLsizei n, const GLuint *pipelines)
{
        assert(0);
}

void mglBindProgramPipeline(GLMContext ctx, GLuint pipeline)
{
        assert(0);
}

void mglUseProgramStages(GLMContext ctx, GLuint pipeline, GLbitfield stages, GLuint program)
{
        assert(0);
}
