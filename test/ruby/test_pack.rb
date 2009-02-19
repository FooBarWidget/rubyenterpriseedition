require 'test/unit'

class TestPack < Test::Unit::TestCase
  def test_pack
    $format = "c2x5CCxsdils_l_a6";
    # Need the expression in here to force ary[5] to be numeric.  This avoids
    # test2 failing because ary2 goes str->numeric->str and ary does not.
    ary = [1,-100,127,128,32767,987.654321098 / 100.0,12345,123456,-32767,-123456,"abcdef"]
    $x = ary.pack($format)
    ary2 = $x.unpack($format)

    assert_equal(ary.length, ary2.length)
    assert_equal(ary.join(':'), ary2.join(':'))
    assert_match(/def/, $x)

    $x = [-1073741825]
    assert_equal($x, $x.pack("q").unpack("q"))

    $x = [-1]
    assert_equal($x, $x.pack("l").unpack("l"))
  end

  def test_pack_N
    assert_equal "\000\000\000\000", [0].pack('N')
    assert_equal "\000\000\000\001", [1].pack('N')
    assert_equal "\000\000\000\002", [2].pack('N')
    assert_equal "\000\000\000\003", [3].pack('N')
    assert_equal "\377\377\377\376", [4294967294].pack('N')
    assert_equal "\377\377\377\377", [4294967295].pack('N')

    assert_equal "\200\000\000\000", [2**31].pack('N')
    assert_equal "\177\377\377\377", [-2**31-1].pack('N')
    assert_equal "\377\377\377\377", [-1].pack('N')

    assert_equal "\000\000\000\001\000\000\000\001", [1,1].pack('N*')
    assert_equal "\000\000\000\001\000\000\000\001\000\000\000\001", [1,1,1].pack('N*')
  end

  def test_unpack_N
    assert_equal 1, "\000\000\000\001".unpack('N')[0]
    assert_equal 2, "\000\000\000\002".unpack('N')[0]
    assert_equal 3, "\000\000\000\003".unpack('N')[0]
    assert_equal 3, "\000\000\000\003".unpack('N')[0]
    assert_equal 4294967295, "\377\377\377\377".unpack('N')[0]
    assert_equal [1,1], "\000\000\000\001\000\000\000\001".unpack('N*')
    assert_equal [1,1,1], "\000\000\000\001\000\000\000\001\000\000\000\001".unpack('N*')
  end

  def test_pack_U
    assert_raises(RangeError) { [-0x40000001].pack("U") }
    assert_raises(RangeError) { [-0x40000000].pack("U") }
    assert_raises(RangeError) { [-1].pack("U") }
    assert_equal "\000", [0].pack("U")
    assert_equal "\374\277\277\277\277\277", [0x3fffffff].pack("U")
    assert_equal "\375\200\200\200\200\200", [0x40000000].pack("U")
    assert_equal "\375\277\277\277\277\277", [0x7fffffff].pack("U")
    assert_raises(RangeError) { [0x80000000].pack("U") }
    assert_raises(RangeError) { [0x100000000].pack("U") }
  end

  def test_pack_unpack_hH
    assert_equal("\x01\xfe", ["10ef"].pack("h*"))
    assert_equal("", ["10ef"].pack("h0"))
    assert_equal("\x01\x0e", ["10ef"].pack("h3"))
    assert_equal("\x01\xfe\x0", ["10ef"].pack("h5"))
    assert_equal("\xff\x0f", ["fff"].pack("h3"))
    assert_equal("\xff\x0f", ["fff"].pack("h4"))
    assert_equal("\xff\x0f\0", ["fff"].pack("h5"))
    assert_equal("\xff\x0f\0", ["fff"].pack("h6"))
    assert_equal("\xff\x0f\0\0", ["fff"].pack("h7"))
    assert_equal("\xff\x0f\0\0", ["fff"].pack("h8"))

    assert_equal("\x10\xef", ["10ef"].pack("H*"))
    assert_equal("", ["10ef"].pack("H0"))
    assert_equal("\x10\xe0", ["10ef"].pack("H3"))
    assert_equal("\x10\xef\x0", ["10ef"].pack("H5"))
    assert_equal("\xff\xf0", ["fff"].pack("H3"))
    assert_equal("\xff\xf0", ["fff"].pack("H4"))
    assert_equal("\xff\xf0\0", ["fff"].pack("H5"))
    assert_equal("\xff\xf0\0", ["fff"].pack("H6"))
    assert_equal("\xff\xf0\0\0", ["fff"].pack("H7"))
    assert_equal("\xff\xf0\0\0", ["fff"].pack("H8"))

    assert_equal(["10ef"], "\x01\xfe".unpack("h*"))
    assert_equal([""], "\x01\xfe".unpack("h0"))
    assert_equal(["1"], "\x01\xfe".unpack("h1"))
    assert_equal(["10"], "\x01\xfe".unpack("h2"))
    assert_equal(["10e"], "\x01\xfe".unpack("h3"))
    assert_equal(["10ef"], "\x01\xfe".unpack("h4"))
    assert_equal(["10ef"], "\x01\xfe".unpack("h5"))

    assert_equal(["10ef"], "\x10\xef".unpack("H*"))
    assert_equal([""], "\x10\xef".unpack("H0"))
    assert_equal(["1"], "\x10\xef".unpack("H1"))
    assert_equal(["10"], "\x10\xef".unpack("H2"))
    assert_equal(["10e"], "\x10\xef".unpack("H3"))
    assert_equal(["10ef"], "\x10\xef".unpack("H4"))
    assert_equal(["10ef"], "\x10\xef".unpack("H5"))
  end
end
