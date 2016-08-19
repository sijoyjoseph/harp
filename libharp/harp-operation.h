/*
 * Copyright (C) 2015-2016 S[&]T, The Netherlands.
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

#ifndef HARP_OPERATION_H
#define HARP_OPERATION_H

#include "harp-internal.h"

typedef enum harp_operation_type_enum
{
    harp_operation_filter_collocation,
    harp_operation_filter_comparison,
    harp_operation_filter_string_comparison,
    harp_operation_filter_bit_mask,
    harp_operation_filter_membership,
    harp_operation_filter_string_membership,
    harp_operation_filter_valid_range,
    harp_operation_filter_longitude_range,
    harp_operation_filter_point_distance,
    harp_operation_filter_area_mask_covers_point,
    harp_operation_filter_area_mask_covers_area,
    harp_operation_filter_area_mask_intersects_area,
    harp_operation_derive_variable,
    harp_operation_keep_variable,
    harp_operation_exclude_variable,
    harp_operation_regrid
} harp_operation_type;

typedef struct harp_operation_struct
{
    harp_operation_type type;
    void *args;
} harp_operation;

typedef enum harp_collocation_filter_type_enum
{
    harp_collocation_left,
    harp_collocation_right
} harp_collocation_filter_type;

typedef enum harp_comparison_operator_type_enum
{
    harp_operator_eq,
    harp_operator_ne,
    harp_operator_lt,
    harp_operator_le,
    harp_operator_gt,
    harp_operator_ge
} harp_comparison_operator_type;

typedef enum harp_bit_mask_operator_type_enum
{
    harp_operator_bit_mask_any,
    harp_operator_bit_mask_none
} harp_bit_mask_operator_type;

typedef enum harp_membership_operator_type_enum
{
    harp_operator_in,
    harp_operator_not_in
} harp_membership_operator_type;

typedef struct harp_collocation_filter_args_struct
{
    char *filename;
    harp_collocation_filter_type filter_type;
} harp_collocation_filter_args;

typedef struct harp_comparison_filter_args_struct
{
    char *variable_name;
    harp_comparison_operator_type operator_type;
    double value;
    char *unit;
} harp_comparison_filter_args;

typedef struct harp_string_comparison_filter_args_struct
{
    char *variable_name;
    harp_comparison_operator_type operator_type;
    char *value;
} harp_string_comparison_filter_args;

typedef struct harp_bit_mask_filter_args_struct
{
    char *variable_name;
    harp_bit_mask_operator_type operator_type;
    uint32_t bit_mask;
} harp_bit_mask_filter_args;

typedef struct harp_membership_filter_args_struct
{
    char *variable_name;
    harp_membership_operator_type operator_type;
    int num_values;
    double *value;
    char *unit;
} harp_membership_filter_args;

typedef struct harp_string_membership_filter_args_struct
{
    char *variable_name;
    harp_membership_operator_type operator_type;
    int num_values;
    char **value;
} harp_string_membership_filter_args;

typedef struct harp_valid_range_filter_args_struct
{
    char *variable_name;
} harp_valid_range_filter_args;

typedef struct harp_longitude_range_filter_args_struct
{
    double min;
    char *min_unit;
    double max;
    char *max_unit;
} harp_longitude_range_filter_args;

typedef struct harp_point_distance_filter_args_struct
{
    double longitude;
    char *longitude_unit;
    double latitude;
    char *latitude_unit;
    double distance;
    char *distance_unit;
} harp_point_distance_filter_args;

typedef struct harp_area_mask_covers_point_filter_args_struct
{
    char *filename;
} harp_area_mask_covers_point_filter_args;

typedef struct harp_area_mask_covers_area_filter_args_struct
{
    char *filename;
} harp_area_mask_covers_area_filter_args;

typedef struct harp_area_mask_intersects_area_filter_args_struct
{
    char *filename;
    double min_percentage;
} harp_area_mask_intersects_area_filter_args;

typedef struct harp_variable_derivation_args_struct
{
    char *variable_name;
    int num_dimensions;
    harp_dimension_type dimension_type[HARP_MAX_NUM_DIMS];
    char *unit;
} harp_variable_derivation_args;

typedef struct harp_variable_inclusion_args_struct
{
    int num_variables;
    char **variable_name;
} harp_variable_inclusion_args;

typedef struct harp_variable_exclusion_args_struct
{
    int num_variables;
    char **variable_name;
} harp_variable_exclusion_args;

typedef struct harp_regrid_args_struct
{
    char *grid_filename;
} harp_regrid_args;

/* Generic operation */
int harp_operation_new(harp_operation_type type, void *args, harp_operation **new_operation);
void harp_operation_delete(harp_operation *operation);
int harp_operation_copy(const harp_operation *other_operation, harp_operation **new_operation);

/* Specific operations */
int harp_collocation_filter_new(const char *filename, harp_collocation_filter_type filter_type,
                                harp_operation **new_operation);

int harp_comparison_filter_new(const char *variable_name, harp_comparison_operator_type operator_type, double value,
                               const char *unit, harp_operation **new_operation);

int harp_string_comparison_filter_new(const char *variable_name, harp_comparison_operator_type operator_type,
                                      const char *value, harp_operation **new_operation);

int harp_bit_mask_filter_new(const char *variable_name, harp_bit_mask_operator_type operator_type, uint32_t bit_mask,
                             harp_operation **new_operation);

int harp_membership_filter_new(const char *variable_name, harp_membership_operator_type operator_type, int num_values,
                               const double *value, const char *unit, harp_operation **new_operation);

int harp_string_membership_filter_new(const char *variable_name, harp_membership_operator_type operator_type,
                                      int num_values, const char **value, harp_operation **new_operation);

int harp_valid_range_filter_new(const char *variable_name, harp_operation **new_operation);

int harp_longitude_range_filter_new(double min, const char *min_unit, double max, const char *max_unit,
                                    harp_operation **new_operation);

int harp_point_distance_filter_new(double longitude, const char *longitude_unit, double latitude,
                                   const char *latitude_unit, double distance, const char *distance_unit,
                                   harp_operation **operation);

int harp_area_mask_covers_point_filter_new(const char *filename, harp_operation **new_operation);

int harp_area_mask_covers_area_filter_new(const char *filename, harp_operation **new_operation);

int harp_area_mask_intersects_area_filter_new(const char *filename, double min_percentage,
                                              harp_operation **new_operation);

int harp_variable_derivation_new(const char *variable_name, int num_dimensions,
                                 const harp_dimension_type *dimension_type, const char *unit,
                                 harp_operation **new_operation);

int harp_variable_inclusion_new(int num_variables, const char **variable_name, harp_operation **new_operation);

int harp_variable_exclusion_new(int num_variables, const char **variable_name, harp_operation **new_operation);

int harp_regrid_new(const char *grid_filename, harp_operation **new_operation);

int harp_operation_get_variable_name(const harp_operation *operation, const char **variable_name);

int harp_operation_is_dimension_filter(const harp_operation *operation);

#endif