# Copyright (C) 2012-2014  Kouhei Sutou <kou@clear-code.com>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License version 2.1 as published by the Free Software Foundation.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

class IndexCursorTest < Test::Unit::TestCase
  include GroongaTestUtils

  def setup
    setup_database
    setup_schema
    setup_records
  end

  def test_open_cursor
    postings = []
    @terms.open_cursor do |table_cursor|
      index_cursor = nil
      @content_index.open_cursor(table_cursor) do |cursor|
        cursor.each do |posting|
          postings << posting.to_hash
        end
        index_cursor = cursor
      end
      assert_predicate(index_cursor, :closed?)
    end

    assert_equal(expected_postings, postings)
  end

  def test_enumerable
    opened = false
    @terms.open_cursor do |table_cursor|
      @content_index.open_cursor(table_cursor) do |cursor|
        postings = cursor.collect do |posting|
          posting.to_hash
        end
        assert_equal(expected_postings, postings)
        opened = true
      end
    end

    assert_true(opened)
  end

  def test_each_without_block
    opened = false
    @terms.open_cursor do |table_cursor|
      @content_index.open_cursor(table_cursor) do |cursor|
        postings = cursor.each.collect(&:to_hash)
        assert_equal(expected_postings, postings)
        opened = true
      end
    end

    assert_true(opened)
  end

  def test_each_reuse_posting_object
    opened = false
    @terms.open_cursor do |table_cursor|
      @content_index.open_cursor(table_cursor) do |cursor|
        posting_object_ids = []
        postings = []
        cursor.each(:reuse_posting_object => true) do |posting|
          posting_object_ids << posting.object_id
          postings << posting.to_hash
        end
        assert_equal([posting_object_ids.first] * posting_object_ids.size,
                     posting_object_ids)
        assert_equal(expected_postings, postings)
        opened = true
      end
    end

    assert_true(opened)
  end

  def test_record
    record = nil
    @terms.open_cursor do |table_cursor|
      @content_index.open_cursor(table_cursor) do |cursor|
        posting = cursor.next
        record = posting.record
      end
    end

    assert_equal("1", record.key)
  end

  def test_term
    term = nil
    @terms.open_cursor do |table_cursor|
      @content_index.open_cursor(table_cursor) do |cursor|
        posting = cursor.next
        term = posting.term
      end
    end

    assert_equal("l", term.key)
  end

  private
  def create_hashes(keys, values)
    hashes = []
    values.each do |value|
      hash = {}
      keys.each_with_index do |key, i|
        hash[key] = value[i]
      end
      hashes << hash
    end
    hashes
  end

  def setup_schema
    Groonga::Schema.define do |schema|
      schema.create_table("Articles",
                          :type => :hash,
                          :key_type => :short_text) do |table|
        table.text("content")
      end

      schema.create_table("Terms",
                          :type => :hash,
                          :default_tokenizer => :bigram_split_symbol_alpha) do |table|
        table.index("Articles.content")
      end
    end

    @articles = Groonga["Articles"]
    @terms = Groonga["Terms"]
    @content_index = Groonga["Terms.Articles_content"]
  end

  def setup_records
    @articles.add("1", :content => "l")
    @articles.add("2", :content => "ll")
    @articles.add("3", :content => "hello")
  end

  def expected_postings
    parameters = [:record_id, :section_id, :term_id, :position,
                  :term_frequency, :weight, :n_rest_postings]

    expected = [
                [1, 1, 1, 0, 1, 0, 0],
                [2, 1, 1, 1, 1, 0, 0],
                [2, 1, 2, 0, 1, 0, 0],
                [3, 1, 2, 2, 1, 0, 0],
                [3, 1, 3, 0, 1, 0, 0],
                [3, 1, 4, 1, 1, 0, 0],
                [3, 1, 5, 3, 1, 0, 0],
                [3, 1, 6, 4, 1, 0, 0]
               ]

    create_hashes(parameters, expected)
  end
end
