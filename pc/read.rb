require "libusb"

usb = LIBUSB::Context.new
device = usb.devices(idVendor: 0x1209, idProduct: 0x0001).first
device.open_interface(0) do |handle|
  loop do
  	t0 = Time.now.to_f
    handle.bulk_transfer(:endpoint => 0x82, :dataIn => 1000000, :timeout => 10000)
	  puts "1MB received in #{Time.now.to_f - t0}"
  end
end