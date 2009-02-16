require 'test/unit'

rcsid = %w$Id$
if rcsid[3]
  Version = rcsid[2].scan(/\d+/).collect!(&method(:Integer)).freeze
  Release = rcsid[3].freeze
end

exit Test::Unit::AutoRunner.run(true, File.dirname($0))
