/*
 * Copyright (C) 2015 S[&]T, The Netherlands.
 *
 * This file is part of HARP.
 *
 * HARP is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * HARP is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HARP; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "harp-internal.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "netcdf.h"

typedef struct harp_dimensions
{
    int num_dimensions;
    harp_dimension_type *type;
    long *length;
} harp_dimensions;

static void dimensions_init(harp_dimensions *dimensions)
{
    dimensions->num_dimensions = 0;
    dimensions->type = NULL;
    dimensions->length = NULL;
}

static void dimensions_done(harp_dimensions *dimensions)
{
    if (dimensions->length != NULL)
    {
        free(dimensions->length);
    }
    if (dimensions->type != NULL)
    {
        free(dimensions->type);
    }
}

/* returns id of dimension matching the specified type (or length for independent dimensions), -1 otherwise */
static int dimensions_find(harp_dimensions *dimensions, harp_dimension_type type, long length)
{
    int i;

    if (type == harp_dimension_independent)
    {
        /* find independent dimension by length */
        for (i = 0; i < dimensions->num_dimensions; i++)
        {
            if (dimensions->type[i] == harp_dimension_independent && dimensions->length[i] == length)
            {
                return i;
            }
        }
    }
    else
    {
        /* find by type */
        for (i = 0; i < dimensions->num_dimensions; i++)
        {
            if (dimensions->type[i] == type)
            {
                return i;
            }
        }
    }

    return -1;
}

/* returns id of new dimension on success, -1 otherwise. */
static int dimensions_add(harp_dimensions *dimensions, harp_dimension_type type, long length)
{
    int index;

    index = dimensions_find(dimensions, type, length);
    if (index >= 0)
    {
        if (dimensions->length[index] != length)
        {
            harp_set_error(HARP_ERROR_INVALID_ARGUMENT, "duplicate dimensions with name '%s' and different sizes "
                           "'%ld' '%ld'", harp_get_dimension_type_name(type), dimensions->length[index], length);
            return -1;
        }
        return index;
    }

    /* dimension does not yet exist -> add it */
    if (dimensions->num_dimensions % BLOCK_SIZE == 0)
    {
        long *new_length;
        harp_dimension_type *new_type;

        new_length = realloc(dimensions->length, (dimensions->num_dimensions + BLOCK_SIZE) * sizeof(long));
        if (new_length == NULL)
        {
            harp_set_error(HARP_ERROR_OUT_OF_MEMORY, "out of memory (could not allocate %lu bytes) (%s:%u)",
                           (dimensions->num_dimensions + BLOCK_SIZE) * sizeof(long), __FILE__, __LINE__);
            return -1;
        }
        dimensions->length = new_length;
        new_type = realloc(dimensions->type, (dimensions->num_dimensions + BLOCK_SIZE) * sizeof(harp_dimension_type));
        if (new_type == NULL)
        {
            harp_set_error(HARP_ERROR_OUT_OF_MEMORY, "out of memory (could not allocate %lu bytes) (%s:%u)",
                           (dimensions->num_dimensions + BLOCK_SIZE) * sizeof(harp_dimension_type), __FILE__, __LINE__);
            return -1;
        }
        dimensions->type = new_type;
    }
    dimensions->type[dimensions->num_dimensions] = type;
    dimensions->length[dimensions->num_dimensions] = length;
    dimensions->num_dimensions++;

    return dimensions->num_dimensions - 1;
}

static harp_data_type get_harp_type(int data_type)
{
    switch (data_type)
    {
        case NC_BYTE:
            return harp_type_int8;
        case NC_SHORT:
            return harp_type_int16;
        case NC_INT:
            return harp_type_int32;
        case NC_FLOAT:
            return harp_type_float;
        case NC_DOUBLE:
            return harp_type_double;
        case NC_CHAR:
            return harp_type_string;
    }
    assert(0);
    exit(1);
}

static int get_netcdf_type(harp_data_type data_type)
{
    switch (data_type)
    {
        case harp_type_int8:
            return NC_BYTE;
        case harp_type_int16:
            return NC_SHORT;
        case harp_type_int32:
            return NC_INT;
        case harp_type_float:
            return NC_FLOAT;
        case harp_type_double:
            return NC_DOUBLE;
        case harp_type_string:
            return NC_CHAR;
    }
    assert(0);
    exit(1);
}

static int read_string_attribute(int ncid, int varid, const char *name, char **data)
{
    char *str;
    nc_type data_type;
    size_t netcdf_num_elements;
    int result;

    result = nc_inq_att(ncid, varid, name, &data_type, &netcdf_num_elements);
    if (result != NC_NOERR)
    {
        harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
        return -1;
    }

    if (data_type != NC_CHAR)
    {
        harp_set_error(HARP_ERROR_PRODUCT, "attribute '%s' has invalid type", name);
        return -1;
    }

    str = malloc((netcdf_num_elements + 1) * sizeof(char));
    if (str == NULL)
    {
        harp_set_error(HARP_ERROR_OUT_OF_MEMORY, "out of memory (could not allocate %lu bytes) (%s:%u)",
                       (netcdf_num_elements + 1) * sizeof(char), __FILE__, __LINE__);
        return -1;
    }

    result = nc_get_att_text(ncid, varid, name, str);
    if (result != NC_NOERR)
    {
        harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
        free(str);
        return -1;
    }
    str[netcdf_num_elements] = '\0';

    *data = str;
    return 0;
}

static int read_scalar_attribute(int ncid, int varid, const char *name, harp_data_type *data_type, harp_scalar *data)
{
    nc_type netcdf_data_type;
    size_t netcdf_num_elements;
    int result;

    result = nc_inq_att(ncid, varid, name, &netcdf_data_type, &netcdf_num_elements);
    if (result != NC_NOERR)
    {
        harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
        return -1;
    }

    if (netcdf_num_elements != 1)
    {
        harp_set_error(HARP_ERROR_PRODUCT, "attribute '%s' has invalid format", name);
        return -1;
    }

    *data_type = get_harp_type(netcdf_data_type);

    switch (netcdf_data_type)
    {
        case NC_BYTE:
            result = nc_get_att_schar(ncid, varid, name, &data->int8_data);
            break;
        case NC_SHORT:
            result = nc_get_att_short(ncid, varid, name, &data->int16_data);
            break;
        case NC_INT:
            result = nc_get_att_int(ncid, varid, name, &data->int32_data);
            break;
        case NC_FLOAT:
            result = nc_get_att_float(ncid, varid, name, &data->float_data);
            break;
        case NC_DOUBLE:
            result = nc_get_att_double(ncid, varid, name, &data->double_data);
            break;
        default:
            harp_set_error(HARP_ERROR_PRODUCT, "attribute '%s' has invalid type", name);
            return -1;
    }

    if (result != NC_NOERR)
    {
        harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
        return -1;
    }

    return 0;
}

static int read_variable(harp_product *product, int ncid, int varid, harp_dimensions *dimensions)
{
    harp_variable *variable;
    harp_data_type data_type;
    int num_dimensions;
    harp_dimension_type dimension_type[HARP_MAX_NUM_DIMS];
    long dimension[HARP_MAX_NUM_DIMS];
    long num_elements;
    char netcdf_name[NC_MAX_NAME + 1];
    nc_type netcdf_data_type;
    int netcdf_num_dimensions;
    int dim_id[NC_MAX_VAR_DIMS];
    int result;
    int i;

    result = nc_inq_var(ncid, varid, netcdf_name, &netcdf_data_type, &netcdf_num_dimensions, dim_id, NULL);
    if (result != NC_NOERR)
    {
        harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
        return -1;
    }

    data_type = get_harp_type(netcdf_data_type);
    num_dimensions = netcdf_num_dimensions;
    if (data_type == harp_type_string && num_dimensions > 0)
    {
        num_dimensions--;
    }
    assert(num_dimensions <= HARP_MAX_NUM_DIMS);

    num_elements = 1;
    for (i = 0; i < num_dimensions; i++)
    {
        dimension_type[i] = dimensions->type[dim_id[i]];
        dimension[i] = dimensions->length[dim_id[i]];
        num_elements *= dimension[i];
    }

    if (harp_variable_new(netcdf_name, data_type, num_dimensions, dimension_type, dimension, &variable) != 0)
    {
        return -1;
    }

    if (harp_product_add_variable(product, variable) != 0)
    {
        harp_variable_delete(variable);
        return -1;
    }

    /* Read data. */
    if (data_type == harp_type_string)
    {
        char *buffer;
        long string_length = 1;

        if (netcdf_num_dimensions > 0)
        {
            string_length = dimensions->length[dim_id[netcdf_num_dimensions - 1]];
        }

        buffer = malloc(num_elements * string_length * sizeof(char));
        if (buffer == NULL)
        {
            harp_set_error(HARP_ERROR_OUT_OF_MEMORY, "out of memory (could not allocate %lu bytes) (%s:%u)",
                           num_elements * string_length * sizeof(char), __FILE__, __LINE__);
            return -1;
        }
        result = nc_get_var_text(ncid, varid, buffer);
        if (result != NC_NOERR)
        {
            harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
            free(buffer);
            return -1;
        }
        for (i = 0; i < num_elements; i++)
        {
            char *str;

            str = malloc((string_length + 1) * sizeof(char));
            if (str == NULL)
            {
                harp_set_error(HARP_ERROR_OUT_OF_MEMORY, "out of memory (could not allocate %lu bytes) (%s:%u)",
                               (string_length + 1) * sizeof(char), __FILE__, __LINE__);
                free(buffer);
                return -1;
            }
            memcpy(str, &buffer[i * string_length], string_length);
            str[string_length] = '\0';
            variable->data.string_data[i] = str;
        }
        free(buffer);
    }
    else
    {
        switch (data_type)
        {
            case harp_type_int8:
                result = nc_get_var_schar(ncid, varid, variable->data.int8_data);
                break;
            case harp_type_int16:
                result = nc_get_var_short(ncid, varid, variable->data.int16_data);
                break;
            case harp_type_int32:
                result = nc_get_var_int(ncid, varid, variable->data.int32_data);
                break;
            case harp_type_float:
                result = nc_get_var_float(ncid, varid, variable->data.float_data);
                break;
            case harp_type_double:
                result = nc_get_var_double(ncid, varid, variable->data.double_data);
                break;
            default:
                assert(0);
                exit(1);
        }
        if (result != NC_NOERR)
        {
            harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
            return -1;
        }
    }

    /* Read attributes. */
    result = nc_inq_att(ncid, varid, "description", NULL, NULL);
    if (result == NC_NOERR)
    {
        if (read_string_attribute(ncid, varid, "description", &variable->description) != 0)
        {
            harp_add_error_message(" (variable '%s')", netcdf_name);
            return -1;
        }
    }
    else if (result != NC_ENOTATT)
    {
        harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
        return -1;
    }

    result = nc_inq_att(ncid, varid, "units", NULL, NULL);
    if (result == NC_NOERR)
    {
        if (read_string_attribute(ncid, varid, "units", &variable->unit) != 0)
        {
            harp_add_error_message(" (variable '%s')", netcdf_name);
            return -1;
        }
    }
    else if (result != NC_ENOTATT)
    {
        harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
        return -1;
    }

    result = nc_inq_att(ncid, varid, "valid_min", NULL, NULL);
    if (result == NC_NOERR)
    {
        harp_data_type attr_data_type;

        if (read_scalar_attribute(ncid, varid, "valid_min", &attr_data_type, &variable->valid_min) != 0)
        {
            harp_add_error_message(" (variable '%s')", netcdf_name);
            return -1;
        }

        if (attr_data_type != data_type)
        {
            harp_set_error(HARP_ERROR_PRODUCT, "attribute 'valid_min' of variable '%s' has invalid type", netcdf_name);
            return -1;
        }
    }
    else if (result != NC_ENOTATT)
    {
        harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
        return -1;
    }

    result = nc_inq_att(ncid, varid, "valid_max", NULL, NULL);
    if (result == NC_NOERR)
    {
        harp_data_type attr_data_type;

        if (read_scalar_attribute(ncid, varid, "valid_max", &attr_data_type, &variable->valid_max) != 0)
        {
            harp_add_error_message(" (variable '%s')", netcdf_name);
            return -1;
        }

        if (attr_data_type != data_type)
        {
            harp_set_error(HARP_ERROR_PRODUCT, "attribute 'valid_max' of variable '%s' has invalid type", netcdf_name);
            return -1;
        }
    }
    else if (result != NC_ENOTATT)
    {
        harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
        return -1;
    }

    return 0;
}

static int verify_product(int ncid)
{
    int result;
    char *convention_str;

    result = nc_inq_att(ncid, NC_GLOBAL, "Conventions", NULL, NULL);
    if (result == NC_NOERR)
    {
        if (read_string_attribute(ncid, NC_GLOBAL, "Conventions", &convention_str) == 0)
        {
            int major, minor;

            if (harp_parse_file_convention(convention_str, &major, &minor) == 0)
            {
                free(convention_str);
                if (major > HARP_FORMAT_VERSION_MAJOR ||
                    (major == HARP_FORMAT_VERSION_MAJOR && minor > HARP_FORMAT_VERSION_MINOR))
                {
                    harp_set_error(HARP_ERROR_UNSUPPORTED_PRODUCT, "unsupported HARP format version %d.%d",
                                   major, minor);
                    return -1;
                }
                return 0;
            }
            free(convention_str);
        }
    }

    harp_set_error(HARP_ERROR_UNSUPPORTED_PRODUCT, "not a valid HARP product");
    return -1;
}

static int read_product(int ncid, harp_product *product, harp_dimensions *dimensions)
{
    int num_dimensions;
    int num_variables;
    int num_attributes;
    int unlim_dim;
    int result;
    int i;

    result = nc_inq(ncid, &num_dimensions, &num_variables, &num_attributes, &unlim_dim);
    if (result != NC_NOERR)
    {
        harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
        return -1;
    }

    for (i = 0; i < num_dimensions; i++)
    {
        harp_dimension_type type;
        int num_consumed = -1;
        char name[NC_MAX_NAME + 1];
        size_t length;

        result = nc_inq_dim(ncid, i, name, &length);
        if (result != NC_NOERR)
        {
            harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
            return -1;
        }

        if (sscanf(name, "independent_%*d%n", &num_consumed) == 0 && (size_t)num_consumed == strlen(name))
        {
            type = harp_dimension_independent;
        }
        else
        {
            if (harp_parse_dimension_type(name, &type) != 0 || type == harp_dimension_independent)
            {
                harp_set_error(HARP_ERROR_PRODUCT, "unsupported dimension '%s'", name);
                return -1;
            }
        }

        if (dimensions_add(dimensions, type, length) != i)
        {
            harp_set_error(HARP_ERROR_PRODUCT, "duplicate dimensions with name '%s'", name);
            return -1;
        }
    }

    for (i = 0; i < num_variables; i++)
    {
        if (read_variable(product, ncid, i, dimensions) != 0)
        {
            return -1;
        }
    }

    result = nc_inq_att(ncid, NC_GLOBAL, "source_product", NULL, NULL);
    if (result == NC_NOERR)
    {
        if (read_string_attribute(ncid, NC_GLOBAL, "source_product", &product->source_product) != 0)
        {
            return -1;
        }
    }
    else if (result != NC_ENOTATT)
    {
        harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
        return -1;
    }

    result = nc_inq_att(ncid, NC_GLOBAL, "history", NULL, NULL);
    if (result == NC_NOERR)
    {
        if (read_string_attribute(ncid, NC_GLOBAL, "history", &product->history) != 0)
        {
            return -1;
        }
    }
    else if (result != NC_ENOTATT)
    {
        harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
        return -1;
    }

    return 0;
}

int harp_import_netcdf(const char *filename, harp_product **product)
{
    harp_product *new_product;
    harp_dimensions dimensions;
    int ncid;
    int result;

    if (filename == NULL)
    {
        harp_set_error(HARP_ERROR_INVALID_ARGUMENT, "filename is NULL (%s:%u)", __FILE__, __LINE__);
        return -1;
    }

    result = nc_open(filename, 0, &ncid);
    if (result != NC_NOERR)
    {
        harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
        return -1;
    }

    if (verify_product(ncid) != 0)
    {
        nc_close(ncid);
        return -1;
    }

    if (harp_product_new(&new_product) != 0)
    {
        nc_close(ncid);
        return -1;
    }

    dimensions_init(&dimensions);

    if (read_product(ncid, new_product, &dimensions) != 0)
    {
        dimensions_done(&dimensions);
        harp_product_delete(new_product);
        nc_close(ncid);
        return -1;
    }

    dimensions_done(&dimensions);

    result = nc_close(ncid);
    if (result != NC_NOERR)
    {
        harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
        harp_product_delete(new_product);
        return -1;
    }

    *product = new_product;
    return 0;
}

int harp_import_global_attributes_netcdf(const char *filename, double *datetime_start, double *datetime_stop,
                                         char **source_product)
{
    char *attr_source_product = NULL;
    double attr_datetime_start = -1;
    double attr_datetime_stop = -1;
    int result;
    int ncid;

    if (filename == NULL)
    {
        harp_set_error(HARP_ERROR_INVALID_ARGUMENT, "filename is NULL (%s:%u)", __FILE__, __LINE__);
        return -1;
    }

    result = nc_open(filename, 0, &ncid);
    if (result != NC_NOERR)
    {
        harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
        return -1;
    }

    if (verify_product(ncid) != 0)
    {
        nc_close(ncid);
        return -1;
    }

    if (datetime_start != NULL)
    {
        harp_data_type attr_data_type;
        harp_scalar attr_data;
        const char *attr_name = "datetime_start";

        if (read_scalar_attribute(ncid, NC_GLOBAL, attr_name, &attr_data_type, &attr_data) != 0)
        {
            nc_close(ncid);
            return -1;
        }

        if (attr_data_type != harp_type_double)
        {
            harp_set_error(HARP_ERROR_PRODUCT, "attribute '%s' has invalid type", attr_name);
            nc_close(ncid);
            return -1;
        }

        attr_datetime_start = attr_data.double_data;
    }

    if (datetime_stop != NULL)
    {
        harp_data_type attr_data_type;
        harp_scalar attr_data;
        const char *attr_name = "datetime_stop";

        if (read_scalar_attribute(ncid, NC_GLOBAL, attr_name, &attr_data_type, &attr_data) != 0)
        {
            nc_close(ncid);
            return -1;
        }

        if (attr_data_type != harp_type_double)
        {
            harp_set_error(HARP_ERROR_PRODUCT, "attribute '%s' has invalid type", attr_name);
            nc_close(ncid);
            return -1;
        }

        attr_datetime_stop = attr_data.double_data;
    }

    if (source_product != NULL)
    {
        if (read_string_attribute(ncid, NC_GLOBAL, "source_product", &attr_source_product) != 0)
        {
            nc_close(ncid);
            return -1;
        }
    }

    result = nc_close(ncid);
    if (result != NC_NOERR)
    {
        harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
        if (attr_source_product != NULL)
        {
            free(attr_source_product);
        }
        return -1;
    }

    if (datetime_start != NULL)
    {
        *datetime_start = attr_datetime_start;
    }

    if (datetime_stop != NULL)
    {
        *datetime_stop = attr_datetime_stop;
    }

    if (source_product != NULL)
    {
        *source_product = attr_source_product;
    }

    return 0;
}

static int write_dimensions(int ncid, const harp_dimensions *dimensions)
{
    int result;
    int i;

    for (i = 0; i < dimensions->num_dimensions; i++)
    {
        int dim_id;

        if (dimensions->type[i] == harp_dimension_independent)
        {
            char name[32];

            sprintf(name, "independent_%ld", dimensions->length[i]);
            result = nc_def_dim(ncid, name, dimensions->length[i], &dim_id);
        }
        else
        {
            result = nc_def_dim(ncid, harp_get_dimension_type_name(dimensions->type[i]), dimensions->length[i],
                                &dim_id);
        }
        if (result != NC_NOERR)
        {
            harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
            return -1;
        }
        assert(dim_id == i);
    }

    return 0;
}

static int write_string_attribute(int ncid, int varid, const char *name, const char *data)
{
    int result;

    result = nc_put_att_text(ncid, varid, name, strlen(data), data);
    if (result != NC_NOERR)
    {
        harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
        return -1;
    }

    return 0;
}

static int write_scalar_attribute(int ncid, int varid, const char *name, harp_data_type data_type, harp_scalar data)
{
    int result;

    result = NC_NOERR;
    switch (data_type)
    {
        case harp_type_int8:
            result = nc_put_att_schar(ncid, varid, name, NC_BYTE, 1, &data.int8_data);
            break;
        case harp_type_int16:
            result = nc_put_att_short(ncid, varid, name, NC_SHORT, 1, &data.int16_data);
            break;
        case harp_type_int32:
            result = nc_put_att_int(ncid, varid, name, NC_INT, 1, &data.int32_data);
            break;
        case harp_type_float:
            result = nc_put_att_float(ncid, varid, name, NC_FLOAT, 1, &data.float_data);
            break;
        case harp_type_double:
            result = nc_put_att_double(ncid, varid, name, NC_DOUBLE, 1, &data.double_data);
            break;
        default:
            assert(0);
            exit(1);
    }

    if (result != NC_NOERR)
    {
        harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
        return -1;
    }

    return 0;
}

static int write_variable_definition(int ncid, const harp_variable *variable, harp_dimensions *dimensions, int *varid)
{
    int num_dimensions;
    int dim_id[NC_MAX_VAR_DIMS];
    int result;
    int i;

    num_dimensions = variable->num_dimensions;
    assert(num_dimensions < NC_MAX_VAR_DIMS);
    for (i = 0; i < num_dimensions; i++)
    {
        dim_id[i] = dimensions_find(dimensions, variable->dimension_type[i], variable->dimension[i]);
        assert(dim_id[i] >= 0);
    }

    if (variable->data_type == harp_type_string)
    {
        long length;

        assert((num_dimensions + 1) < NC_MAX_VAR_DIMS);

        /* determine length for the string dimensions (ensure minimum length of 1) */
        length = harp_array_get_max_string_length(variable->data, variable->num_elements);
        if (length == 0)
        {
            length = 1;
        }
        dim_id[num_dimensions] = dimensions_find(dimensions, harp_dimension_independent, length);
        assert(dim_id[num_dimensions] >= 0);
        num_dimensions++;
    }

    result = nc_def_var(ncid, variable->name, get_netcdf_type(variable->data_type), num_dimensions, dim_id, varid);
    if (result != NC_NOERR)
    {
        harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
        return -1;
    }

    if (variable->description != NULL)
    {
        if (write_string_attribute(ncid, *varid, "description", variable->description) != 0)
        {
            return -1;
        }
    }

    if (variable->unit != NULL)
    {
        if (write_string_attribute(ncid, *varid, "units", variable->unit) != 0)
        {
            return -1;
        }
    }

    if (variable->data_type != harp_type_string)
    {
        if (!harp_is_valid_min_for_type(variable->data_type, variable->valid_min))
        {
            if (write_scalar_attribute(ncid, *varid, "valid_min", variable->data_type, variable->valid_min) != 0)
            {
                return -1;
            }
        }

        if (!harp_is_valid_max_for_type(variable->data_type, variable->valid_max))
        {
            if (write_scalar_attribute(ncid, *varid, "valid_max", variable->data_type, variable->valid_max) != 0)
            {
                return -1;
            }
        }
    }

    return 0;
}

static int write_variable(int ncid, int varid, const harp_variable *variable)
{
    int result = NC_NOERR;

    switch (variable->data_type)
    {
        case harp_type_int8:
            result = nc_put_var_schar(ncid, varid, variable->data.ptr);
            break;
        case harp_type_int16:
            result = nc_put_var_short(ncid, varid, variable->data.ptr);
            break;
        case harp_type_int32:
            result = nc_put_var_int(ncid, varid, variable->data.ptr);
            break;
        case harp_type_float:
            result = nc_put_var_float(ncid, varid, variable->data.ptr);
            break;
        case harp_type_double:
            result = nc_put_var_double(ncid, varid, variable->data.ptr);
            break;
        case harp_type_string:
            {
                char *buffer;

                buffer = harp_array_get_char_array_from_strings(variable->data, variable->num_elements, NULL);
                if (buffer == NULL)
                {
                    return -1;
                }
                result = nc_put_var_text(ncid, varid, buffer);
                free(buffer);
            }
            break;
    }
    if (result != NC_NOERR)
    {
        harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
        return -1;
    }

    return 0;
}

static int write_product(int ncid, const harp_product *product, harp_dimensions *dimensions)
{
    harp_scalar datetime_start;
    harp_scalar datetime_stop;
    int result;
    int i;

    /* write conventions */
    if (write_string_attribute(ncid, NC_GLOBAL, "Conventions", HARP_CONVENTION) != 0)
    {
        return -1;
    }

    /* write datetime range */
    if (harp_product_get_datetime_range(product, &datetime_start.double_data, &datetime_stop.double_data) != 0)
    {
        return -1;
    }

    if (write_scalar_attribute(ncid, NC_GLOBAL, "datetime_start", harp_type_double, datetime_start) != 0)
    {
        return -1;
    }

    if (write_scalar_attribute(ncid, NC_GLOBAL, "datetime_stop", harp_type_double, datetime_stop) != 0)
    {
        return -1;
    }

    /* determine dimensions */
    for (i = 0; i < product->num_variables; i++)
    {
        harp_variable *variable;
        int j;

        variable = product->variable[i];
        for (j = 0; j < variable->num_dimensions; j++)
        {
            if (dimensions_add(dimensions, variable->dimension_type[j], variable->dimension[j]) < 0)
            {
                return -1;
            }
        }
        if (variable->data_type == harp_type_string)
        {
            long length;

            /* determine length for the string dimensions (ensure minimum length of 1) */
            length = harp_array_get_max_string_length(variable->data, variable->num_elements);
            if (length == 0)
            {
                length = 1;
            }
            if (dimensions_add(dimensions, harp_dimension_independent, length) < 0)
            {
                return -1;
            }
        }
    }

    /* write dimensions */
    if (write_dimensions(ncid, dimensions) != 0)
    {
        return -1;
    }

    /* write variable definitions + attributes */
    for (i = 0; i < product->num_variables; i++)
    {
        int varid;

        if (write_variable_definition(ncid, product->variable[i], dimensions, &varid) != 0)
        {
            return -1;
        }
        assert(varid == i);
    }

    /* write product attributes */
    if (product->source_product != NULL)
    {
        if (write_string_attribute(ncid, NC_GLOBAL, "source_product", product->source_product) != 0)
        {
            return -1;
        }
    }

    if (product->history != NULL)
    {
        if (write_string_attribute(ncid, NC_GLOBAL, "history", product->history) != 0)
        {
            return -1;
        }
    }

    result = nc_enddef(ncid);
    if (result != NC_NOERR)
    {
        harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
        return -1;
    }

    /* write variable data */
    for (i = 0; i < product->num_variables; i++)
    {
        if (write_variable(ncid, i, product->variable[i]) != 0)
        {
            return -1;
        }
    }

    return 0;
}

int harp_export_netcdf(const char *filename, const harp_product *product)
{
    harp_dimensions dimensions;
    int result;
    int ncid;

    if (filename == NULL)
    {
        harp_set_error(HARP_ERROR_FILE_OPEN, "could not open netCDF export file (can not write netCDF data to stdout)");
        return -1;
    }

    result = nc_create(filename, 0, &ncid);
    if (result != NC_NOERR)
    {
        harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
        return -1;
    }

    dimensions_init(&dimensions);

    if (write_product(ncid, product, &dimensions) != 0)
    {
        nc_close(ncid);
        dimensions_done(&dimensions);
        return -1;
    }

    dimensions_done(&dimensions);

    result = nc_close(ncid);
    if (result != NC_NOERR)
    {
        harp_set_error(HARP_ERROR_NETCDF, "%s", nc_strerror(result));
        return -1;
    }

    return 0;
}
