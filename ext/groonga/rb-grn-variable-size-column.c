/* -*- coding: utf-8; mode: C; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
  Copyright (C) 2009-2014  Kouhei Sutou <kou@clear-code.com>

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License version 2.1 as published by the Free Software Foundation.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "rb-grn.h"

#define SELF(object) ((RbGrnVariableSizeColumn *)DATA_PTR(object))

VALUE rb_cGrnVariableSizeColumn;

void
rb_grn_variable_size_column_bind (RbGrnVariableSizeColumn *rb_column,
                                  grn_ctx *context, grn_obj *column)
{
    RbGrnObject *rb_grn_object;
    int column_type;
    unsigned char value_type;

    rb_grn_object = RB_GRN_OBJECT(rb_column);
    rb_grn_column_bind(RB_GRN_COLUMN(rb_column), context, column);

    rb_column->element_value = NULL;
    column_type = (column->header.flags & GRN_OBJ_COLUMN_TYPE_MASK);
    if (column_type != GRN_OBJ_COLUMN_VECTOR) {
        return;
    }

    switch (rb_grn_object->range->header.type) {
    case GRN_TABLE_HASH_KEY:
    case GRN_TABLE_PAT_KEY:
    case GRN_TABLE_DAT_KEY:
    case GRN_TABLE_NO_KEY:
        value_type = GRN_UVECTOR;
        break;
    default:
        value_type = GRN_VECTOR;
        break;
    }
    if (column->header.flags & GRN_OBJ_WITH_WEIGHT) {
        rb_column->element_value = grn_obj_open(context, value_type, 0,
                                                rb_grn_object->range_id);
    }
}

void
rb_grn_variable_size_column_finalizer (grn_ctx *context, grn_obj *grn_object,
                                       RbGrnVariableSizeColumn *rb_column)
{
    rb_grn_column_finalizer(context, grn_object,
                            RB_GRN_COLUMN(rb_column));
    if (context && rb_column->element_value)
        grn_obj_unlink(context, rb_column->element_value);
    rb_column->element_value = NULL;
}

static void
rb_grn_variable_size_column_deconstruct (RbGrnVariableSizeColumn *rb_column,
                                         grn_obj **column,
                                         grn_ctx **context,
                                         grn_id *domain_id,
                                         grn_obj **domain,
                                         grn_obj **value,
                                         grn_obj **element_value,
                                         grn_id *range_id,
                                         grn_obj **range)
{
    RbGrnColumn *rb_grn_column;

    rb_grn_column = RB_GRN_COLUMN(rb_column);
    rb_grn_column_deconstruct(rb_grn_column, column, context,
                              domain_id, domain, value,
                              range_id, range);

    if (element_value)
        *element_value = rb_column->element_value;
}

/*
 * Document-class: Groonga::VariableSizeColumn < Groonga::Column
 *
 * A column for variable size data like text family types and vector
 * column.
 */

/*
 * It gets a value of variable size column value for the record that
 * ID is _id_.
 *
 * @example Gets weight vector value
 *    Groonga::Schema.define do |schema|
 *      schema.create_table("Products",
 *                          :type => :patricia_trie,
 *                          :key_type => "ShortText") do |table|
 *        # This is weight vector.
 *        # ":with_weight => true" is important to store weight value.
 *        table.short_text("tags",
 *                         :type => :vector,
 *                         :with_weight => true)
 *      end
 *    end
 *
 *    products = Groonga["Products"]
 *    rroonga = products.add("Rroonga")
 *    rroonga.tags = [
 *      {
 *        :value  => "ruby",
 *        :weight => 100,
 *      },
 *      {
 *        :value  => "groonga",
 *        :weight => 10,
 *      },
 *    ]
 *
 *    p rroonga.tags
 *    # => [
 *    #      {:value => "ruby",    :weight => 100},
 *    #      {:value => "groonga", :weight => 10}
 *    #    ]
 *
 * @overload [](id)
 *   @param [Integer, Record] id The record ID.
 *   @return [Array<Hash<Symbol, String>>] An array of value if the column
 *     is a weight vector column.
 *     Each value is a Hash like the following form:
 *
 *     <pre>
 *     {
 *       :value  => [KEY],
 *       :weight => [WEIGHT],
 *     }
 *     </pre>
 *
 *     @[KEY]@ is the key of the table that is specified as range on
 *     creating the weight vector.
 *
 *     @[WEIGHT]@ is a positive integer.
 *
 *   @return [::Object] See {Groonga::Object#[]} for columns except
 *     weight vector column.
 *
 * @since 4.0.1.
 */
static VALUE
rb_grn_variable_size_column_array_reference (VALUE self, VALUE rb_id)
{
    grn_ctx *context = NULL;
    grn_obj *column, *range;
    grn_id id;
    grn_obj *value;
    VALUE rb_value;
    unsigned int i, n;

    rb_grn_variable_size_column_deconstruct(SELF(self), &column, &context,
                                            NULL, NULL, &value, NULL,
                                            NULL, &range);

    if (!(column->header.flags & GRN_OBJ_WITH_WEIGHT)) {
        return rb_call_super(1, &rb_id);
    }

    id = RVAL2GRNID(rb_id, context, range, self);

    grn_obj_reinit(context, value,
                   value->header.domain,
                   value->header.flags | GRN_OBJ_VECTOR);
    grn_obj_get_value(context, column, id, value);
    rb_grn_context_check(context, self);

    n = grn_vector_size(context, value);
    rb_value = rb_ary_new2(n);
    for (i = 0; i < n; i++) {
        const char *element_value;
        unsigned int element_value_length;
        unsigned int weight = 0;
        grn_id domain;
        VALUE rb_element;

        element_value_length = grn_vector_get_element(context,
                                                      value,
                                                      i,
                                                      &element_value,
                                                      &weight,
                                                      &domain);
        rb_element = rb_hash_new();
        rb_hash_aset(rb_element,
                     ID2SYM(rb_intern("value")),
                     rb_str_new(element_value, element_value_length));
        rb_hash_aset(rb_element,
                     ID2SYM(rb_intern("weight")),
                     UINT2NUM(weight));

        rb_ary_push(rb_value, rb_element);
    }

    return rb_value;
}

/*
 * It updates a value of variable size column value for the record
 * that ID is _id_.
 *
 * Weight vector column is a special variable size column. This
 * description describes only weight vector column. Other variable
 * size column works what you think.
 *
 * @example Use weight vector as matrix search result weight
 *    Groonga::Schema.define do |schema|
 *      schema.create_table("Products",
 *                          :type => :patricia_trie,
 *                          :key_type => "ShortText") do |table|
 *        # This is weight vector.
 *        # ":with_weight => true" is important for matrix search result weight.
 *        table.short_text("Tags",
 *                         :type => :vector,
 *                         :with_weight => true)
 *      end
 *
 *      schema.create_table("Tags",
 *                          :type => :hash,
 *                          :key_type => "ShortText") do |table|
 *        # This is inverted index. It also needs ":with_weight => true".
 *        table.index("Products.tags", :with_weight => true)
 *      end
 *    end
 *
 *    products = Groonga["Products"]
 *    groonga = products.add("Groonga")
 *    groonga.tags = [
 *      {
 *        :value  => "groonga",
 *        :weight => 100,
 *      },
 *    ]
 *    rroonga = products.add("Rroonga")
 *    rroonga.tags = [
 *      {
 *        :value  => "ruby",
 *        :weight => 100,
 *      },
 *      {
 *        :value  => "groonga",
 *        :weight => 10,
 *      },
 *    ]
 *
 *    result = products.select do |record|
 *      # Search by "groonga"
 *      record.match("groonga") do |match_target|
 *        match_target.tags
 *      end
 *    end
 *
 *    result.each do |record|
 *      p [record.key.key, record.score]
 *    end
 *    # Matches all records with weight.
 *    # => ["Groonga", 100]
 *    #    ["Rroonga", 10]
 *
 *    # Increases score for "ruby" 10 times
 *    products.select(# The previous search result. Required.
 *                    :result => result,
 *                    # It just adds score to existing records in the result. Required.
 *                    :operator => Groonga::Operator::ADJUST) do |record|
 *      record.match("ruby") do |target|
 *        target.tags * 10 # 10 times
 *      end
 *    end
 *
 *    result.each do |record|
 *      p [record.key.key, record.score]
 *    end
 *    # Weight is used for increasing score.
 *    # => ["Groonga", 100]  <- Not changed.
 *    #    ["Rroonga", 1010] <- 1000 (= 100 * 10) increased.
 *
 * @overload []=(id, elements)
 *   This description is for weight vector column.
 *
 *   @param [Integer, Record] id The record ID.
 *   @param [Array<Hash<Symbol, String>>] elements An array of values
 *     for weight vector.
 *     Each value is a Hash like the following form:
 *
 *     <pre>
 *     {
 *       :value  => [KEY],
 *       :weight => [WEIGHT],
 *     }
 *     </pre>
 *
 *     @[KEY]@ must be the same type of the key of the table that is
 *     specified as range on creating the weight vector.
 *
 *     @[WEIGHT]@ must be an positive integer.
 *
 * @overload []=(id, value)
 *   This description is for variable size columns except weight
 *   vector column.
 *
 *   @param [Integer, Record] id The record ID.
 *   @param [::Object] value A new value.
 *   @see Groonga::Object#[]=
 *
 * @since 4.0.1
 */
static VALUE
rb_grn_variable_size_column_array_set (VALUE self, VALUE rb_id, VALUE rb_value)
{
    grn_ctx *context = NULL;
    grn_obj *column, *range;
    grn_rc rc;
    grn_id id;
    grn_obj *value, *element_value;
    int i, n;
    int flags = GRN_OBJ_SET;

    rb_grn_variable_size_column_deconstruct(SELF(self), &column, &context,
                                            NULL, NULL, &value, &element_value,
                                            NULL, &range);

    if (!(column->header.flags & GRN_OBJ_WITH_WEIGHT)) {
        VALUE args[2];
        args[0] = rb_id;
        args[1] = rb_value;
        return rb_call_super(2, args);
    }

    id = RVAL2GRNID(rb_id, context, range, self);

    if (!RVAL2CBOOL(rb_obj_is_kind_of(rb_value, rb_cArray))) {
        rb_raise(rb_eArgError,
                 "<%s>: "
                 "weight vector value must be an array of index value: <%s>",
                 rb_grn_inspect(self),
                 rb_grn_inspect(rb_value));
    }

    grn_obj_reinit(context, value,
                   value->header.domain,
                   value->header.flags | GRN_OBJ_VECTOR);
    n = RARRAY_LEN(rb_value);
    for (i = 0; i < n; i++) {
        unsigned int weight = 0;
        VALUE rb_element_value, rb_weight;

        rb_grn_scan_options(RARRAY_PTR(rb_value)[i],
                            "value", &rb_element_value,
                            "weight", &rb_weight,
                            NULL);

        if (!NIL_P(rb_weight)) {
            weight = NUM2UINT(rb_weight);
        }

        GRN_BULK_REWIND(element_value);
        if (!NIL_P(rb_element_value)) {
            RVAL2GRNBULK(rb_element_value, context, element_value);
        }

        grn_vector_add_element(context, value,
                               GRN_BULK_HEAD(element_value),
                               GRN_BULK_VSIZE(element_value),
                               weight,
                               element_value->header.domain);
    }
    rc = grn_obj_set_value(context, column, id, value, flags);
    rb_grn_context_check(context, self);
    rb_grn_rc_check(rc, self);

    return rb_value;
}

/*
 * Returns whether the column is compressed or not. If
 * @type@ is specified, it returns whether the column is
 * compressed by @type@ or not.
 * @overload compressed?
 *   @return [Boolean] whether the column is compressed or not.
 * @overload compressed?(type)
 *   @param [:zlib, :lzo] type (nil)
 *   @return [Boolean] whether specified compressed type is used or not.
 * @since 1.3.1
 */
static VALUE
rb_grn_variable_size_column_compressed_p (int argc, VALUE *argv, VALUE self)
{
    RbGrnVariableSizeColumn *rb_grn_column;
    grn_ctx *context = NULL;
    grn_obj *column;
    grn_obj_flags flags;
    VALUE type;
    grn_bool compressed_p = GRN_FALSE;
    grn_bool accept_any_type = GRN_FALSE;
    grn_bool need_zlib_check = GRN_FALSE;
    grn_bool need_lzo_check = GRN_FALSE;

    rb_scan_args(argc, argv, "01", &type);

    if (NIL_P(type)) {
        accept_any_type = GRN_TRUE;
    } else {
        if (rb_grn_equal_option(type, "zlib")) {
            need_zlib_check = GRN_TRUE;
        } else if (rb_grn_equal_option(type, "lzo")) {
            need_lzo_check = GRN_TRUE;
        } else {
            rb_raise(rb_eArgError,
                     "compressed type should be <:zlib> or <:lzo>: <%s>",
                     rb_grn_inspect(type));
        }
    }

    rb_grn_column = SELF(self);
    rb_grn_object_deconstruct(RB_GRN_OBJECT(rb_grn_column), &column, &context,
                              NULL, NULL,
                              NULL, NULL);

    flags = column->header.flags;
    switch (flags & GRN_OBJ_COMPRESS_MASK) {
      case GRN_OBJ_COMPRESS_ZLIB:
        if (accept_any_type || need_zlib_check) {
            grn_obj support_p;
            GRN_BOOL_INIT(&support_p, 0);
            grn_obj_get_info(context, NULL, GRN_INFO_SUPPORT_ZLIB, &support_p);
            compressed_p = GRN_BOOL_VALUE(&support_p);
        }
        break;
      case GRN_OBJ_COMPRESS_LZO:
        if (accept_any_type || need_lzo_check) {
            grn_obj support_p;
            GRN_BOOL_INIT(&support_p, 0);
            grn_obj_get_info(context, NULL, GRN_INFO_SUPPORT_LZO, &support_p);
            compressed_p = GRN_BOOL_VALUE(&support_p);
        }
        break;
    }

    return CBOOL2RVAL(compressed_p);
}

/*
 * Defrags the column.
 *
 * @overload defrag(options={})
 *   @param options [::Hash] The name and value
 *     pairs. Omitted names are initialized as the default value.
 *   @option options [Integer] :threshold (0) the threshold to
 *     determine whether a segment is defraged. Available
 *     values are -4..22. -4 means all segments are defraged.
 *     22 means no segment is defraged.
 * @return [Integer] the number of defraged segments
 * @since 1.2.6
 */
static VALUE
rb_grn_variable_size_column_defrag (int argc, VALUE *argv, VALUE self)
{
    RbGrnVariableSizeColumn *rb_grn_column;
    grn_ctx *context = NULL;
    grn_obj *column;
    int n_segments;
    VALUE options, rb_threshold;
    int threshold = 0;

    rb_scan_args(argc, argv, "01", &options);
    rb_grn_scan_options(options,
                        "threshold", &rb_threshold,
                        NULL);
    if (!NIL_P(rb_threshold)) {
        threshold = NUM2INT(rb_threshold);
    }

    rb_grn_column = SELF(self);
    rb_grn_object_deconstruct(RB_GRN_OBJECT(rb_grn_column), &column, &context,
                              NULL, NULL,
                              NULL, NULL);
    n_segments = grn_obj_defrag(context, column, threshold);
    rb_grn_context_check(context, self);

    return INT2NUM(n_segments);
}

void
rb_grn_init_variable_size_column (VALUE mGrn)
{
    rb_cGrnVariableSizeColumn =
        rb_define_class_under(mGrn, "VariableSizeColumn", rb_cGrnColumn);

    rb_define_method(rb_cGrnVariableSizeColumn, "[]",
                     rb_grn_variable_size_column_array_reference, 1);
    rb_define_method(rb_cGrnVariableSizeColumn, "[]=",
                     rb_grn_variable_size_column_array_set, 2);

    rb_define_method(rb_cGrnVariableSizeColumn, "compressed?",
                     rb_grn_variable_size_column_compressed_p, -1);
    rb_define_method(rb_cGrnVariableSizeColumn, "defrag",
                     rb_grn_variable_size_column_defrag, -1);
}
