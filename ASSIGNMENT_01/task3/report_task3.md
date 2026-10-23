# ASSIGNMENT-01
## Task 3: Báo cáo Phân tích và Thiết kế Driver

- Họ và tên: Trần Gia Khanh
- MSSV: SE200963
- Môn học: LOS201

---

# 1. Sơ đồ kiến trúc

## 1.1. Kiến trúc tổng thể
### Sơ đồ kiến trúc tổng thể

```text
                  USERSPACE
        +---------------------------+
        | Application Task 1        |
        | asgn1_test                |
        +-------------+-------------+
                      |
             open/read/write/ioctl
                      |
        +-------------v-------------+
        |       System Calls        |
        +-------------+-------------+
                      |
        +-------------v-------------+
        | VFS - file_operations     |
        +-------------+-------------+
                      |
             +--------+--------+
             |                 |
   +---------v--------+ +------v-----------+
   | asgn1_driver     | | counter_driver   |
   | register_chrdev  | | misc_register    |
   | unlocked_ioctl   | | atomic_t counter |
   +---------+--------+ +------+-----------+
             |                 |
   +---------v--------+ +------v-----------+
   | Device /asgn1    | | Device /counter  |
   | procfs / sysfs   | | /proc/counter_info|
   +------------------+ +------------------+
                      |
                Linux Kernel
```
Task 1 sử dụng character device driver được đăng ký bằng
register_chrdev(). Ứng dụng userspace truy cập thiết bị thông qua
các system call như open(), read(), write() và ioctl().

Task 2 sử dụng misc device framework. Driver đăng ký thiết bị bằng
misc_register(), cung cấp các hàm xử lý read() và write(), đồng thời
sử dụng atomic_t để quản lý counter trong môi trường SMP.

Hai driver cung cấp giao diện userspace thông qua device node và
các giao diện thống kê procfs. Task 1 còn cung cấp các thuộc tính
sysfs theo thiết kế của driver.
## 1.2. Luồng xử lý ioctl
### Sơ đồ luồng xử lý ioctl

```text
Userspace application
        |
        v
ioctl(fd, command, argument)
        |
        v
ioctl() system call
        |
        v
VFS / file_operations
        |
        v
unlocked_ioctl()
        |
        v
asgn1_ioctl()
        |
        v
Kiểm tra command
        |
        +--> ASGN1_RESET_BUFFER
        |
        +--> ASGN1_GET_STATS
        |
        +--> ASGN1_SET_MODE
        |
        +--> ASGN1_GET_VERSION
        |
        v
copy_to_user() / copy_from_user()
(khi cần truyền dữ liệu)
        |
        v
Trả kết quả về userspace
```
Ứng dụng userspace gọi hàm ioctl() để yêu cầu driver thực hiện
một thao tác điều khiển thiết bị. Lời gọi này đi qua system call
interface và VFS trước khi được chuyển đến hàm xử lý ioctl của driver.

Trong Task 1, trường unlocked_ioctl trong struct file_operations
được gán cho hàm asgn1_ioctl(). Hàm này kiểm tra mã lệnh và xử lý
một trong bốn yêu cầu:

- ASGN1_RESET_BUFFER: xóa dữ liệu trong buffer và reset các counter.
- ASGN1_GET_STATS: lấy thống kê và sao chép struct về userspace.
- ASGN1_SET_MODE: thiết lập operation mode.
- ASGN1_GET_VERSION: trả về chuỗi phiên bản của driver.

Các thao tác truyền dữ liệu giữa kernel và userspace sử dụng
copy_to_user() hoặc copy_from_user() khi cần thiết. Mutex được
sử dụng để bảo vệ dữ liệu dùng chung trong driver.
---

# 2. So sánh ba loại Device Driver
| Tiêu chí | Character Device | Block Device | Network Device |
|---|---|---|---|
| Giao diện với kernel/VFS | Thường cung cấp các thao tác như open(), read(), write() thông qua file_operations. | Truy cập dữ liệu theo các block/sector, sử dụng block I/O subsystem. | Sử dụng network subsystem và các thao tác của net_device, không hoạt động như file thông thường để truyền packet. |
| Đơn vị truyền dữ liệu | Luồng byte (byte stream). | Block có kích thước xác định, thường theo sector. | Packet/frame. |
| Buffering | Tùy driver và thiết bị; có thể dùng buffer trong kernel hoặc buffer của ứng dụng. | Thường có cơ chế caching và buffering của block layer. | Có hàng đợi packet và các buffer phục vụ truyền nhận mạng. |
| Ví dụ thực tế | Cổng serial, thiết bị ký tự tự tạo như /dev/asgn1. | HDD, SSD, thiết bị lưu trữ USB. | Card Ethernet, Wi-Fi adapter. |
| Trường hợp sử dụng | Khi cần trao đổi dữ liệu dạng luồng hoặc điều khiển thiết bị qua các thao tác file. | Khi cần đọc, ghi và quản lý dữ liệu lưu trữ theo block. | Khi cần gửi và nhận dữ liệu qua mạng. |
### Nhận xét

Character device phù hợp với các thiết bị trao đổi dữ liệu dạng luồng
hoặc cung cấp giao diện điều khiển đơn giản. Trong Task 1, driver sử dụng
character device để cung cấp các thao tác đọc, ghi và ioctl.

Block device phù hợp với các thiết bị lưu trữ cần truy cập dữ liệu theo
block hoặc sector. Cơ chế block I/O hỗ trợ việc quản lý các yêu cầu
đọc ghi dữ liệu lưu trữ.

Network device phù hợp với việc truyền nhận packet qua mạng. Driver
tương tác với network subsystem để gửi và nhận dữ liệu.

Việc lựa chọn loại driver phụ thuộc vào chức năng thiết bị, cách biểu diễn
dữ liệu và subsystem phù hợp của Linux kernel.
---

# 3. Phân tích thiết kế hai driver

## 3.1. Task 1: Character Device Driver
Task 1 sử dụng register_chrdev() để đăng ký character device.
Driver cung cấp các thao tác open(), read(), write() và ioctl()
thông qua struct file_operations.

Khi ứng dụng mở device node, kernel chuyển yêu cầu đến hàm
xử lý tương ứng trong driver. Buffer dùng để lưu dữ liệu được
ghi từ userspace và trả lại khi ứng dụng đọc thiết bị.

Driver còn cung cấp giao diện thống kê qua procfs và các thuộc
tính sysfs để userspace theo dõi trạng thái hoạt động.

Ưu điểm của cách đăng ký này là phù hợp để xây dựng character
device với các thao tác tùy chỉnh. Tuy nhiên, lập trình viên phải
quản lý việc đăng ký và hủy đăng ký thiết bị cũng như các tài
nguyên liên quan.

## 3.2. Task 2: Misc Device Counter Driver
Task 2 sử dụng misc device framework thông qua misc_register().
Framework này giúp đăng ký các thiết bị ký tự đơn giản mà không
cần tự quản lý toàn bộ quá trình đăng ký major number như cách
register_chrdev() thường dùng.

Driver counter cung cấp thiết bị /dev/counter, hỗ trợ đọc giá trị
counter và ghi giá trị mới hoặc reset counter về 0.

Driver sử dụng atomic_t để quản lý counter, tổng số lần đọc và
tổng số lần ghi. Các hàm atomic_read(), atomic_set() và
atomic_inc() được dùng để đọc, cập nhật hoặc tăng các biến
nguyên tử.

Ngoài ra, driver tạo /proc/counter_info để hiển thị giá trị counter,
tổng số lần đọc và tổng số lần ghi.
## 3.3. Phân tích atomic_t và race condition
Race condition có thể xảy ra khi nhiều luồng hoặc CPU cùng truy
cập và cập nhật một biến dùng chung mà không có cơ chế bảo vệ
phù hợp. Kết quả có thể không chính xác nếu các thao tác cập nhật
xung đột với nhau.

Trong Task 2, counter được khai báo bằng atomic_t. Các thao tác
atomic_read(), atomic_set() và atomic_inc() giúp thực hiện các
thao tác nguyên tử trên biến counter, tránh lỗi cập nhật cạnh
tranh đối với những thao tác này.

Việc sử dụng atomic_t phù hợp với counter đơn giản cần được
truy cập trong môi trường SMP. Tuy nhiên, atomic_t không tự động
bảo vệ một nhóm nhiều thao tác hoặc nhiều biến cần được cập nhật
như một giao dịch thống nhất.
## 3.4. Phân tích ioctl
ioctl() cho phép ứng dụng userspace gửi yêu cầu điều khiển đến
driver ngoài các thao tác đọc và ghi dữ liệu thông thường.

Trong Task 1, các lệnh ioctl được định nghĩa trong file header
asgn1_ioctl.h bằng các macro _IO(), _IOR() và _IOW().

- ASGN1_RESET_BUFFER sử dụng _IO() vì không truyền dữ liệu
  giữa userspace và kernel.
- ASGN1_GET_STATS sử dụng _IOR() để trả cấu trúc thống kê
  từ kernel về userspace.
- ASGN1_SET_MODE sử dụng _IOW() để nhận chế độ hoạt động
  từ userspace.
- ASGN1_GET_VERSION sử dụng _IOR() để trả chuỗi phiên bản
  của driver về userspace.

Các macro ioctl mã hóa thông tin về loại lệnh, số thứ tự và
hướng truyền dữ liệu. Với _IOR() và _IOW(), thông tin kích thước
kiểu dữ liệu cũng được mã hóa trong mã lệnh.

Hàm asgn1_ioctl() kiểm tra command và xử lý yêu cầu tương ứng.
Các hàm copy_to_user() và copy_from_user() được sử dụng để
truyền dữ liệu an toàn giữa kernel và userspace. Nếu command
không được hỗ trợ, driver trả về -ENOTTY.
---

# 4. Kết quả kiểm thử

## 4.1. Kiểm thử Task 1
### Kết quả kiểm thử Task 1

1. Nạp module `asgn1_driver.ko` thành công.
2. Tạo device node `/dev/asgn1` với major number 240 và minor number 0.
3. `ASGN1_GET_STATS`: trả về open_count, write_count, read_count,
   buffer_len và last_write_size.
4. `ASGN1_SET_MODE`: đặt operation mode bằng 0 thành công.
5. `ASGN1_GET_VERSION`: trả về chuỗi `asgn1_driver v1.0 - MSSV`.
6. `ASGN1_RESET_BUFFER`: thực hiện thành công.
7. Gỡ module bằng `rmmod asgn1_driver` thành công.

Log kernel xác nhận cả bốn lệnh ioctl được gọi và thiết bị
được đóng trước khi module được gỡ.
## 4.2. Kiểm thử Task 2
Driver `counter_driver.ko` được kiểm thử trên QEMU.

1. Nạp module bằng `insmod counter_driver.ko`. Kernel log xác nhận driver đăng ký thiết bị với major 10, minor 125 và tạo `/proc/counter_info`.
2. Tạo thiết bị bằng `mknod /dev/counter c 10 125`, sau đó cấp quyền bằng `chmod 666 /dev/counter`.
3. Đọc `/dev/counter` để kiểm tra giá trị counter ban đầu.
4. Ghi giá trị 100 vào thiết bị và đọc lại để xác nhận giá trị đã được cập nhật.
5. Ghi giá trị 0 để đặt lại counter.
6. Thử ghi `-1` và `abc`; driver từ chối dữ liệu không hợp lệ với lỗi `Invalid argument`.
7. Gỡ module bằng `rmmod counter_driver`; kernel log xác nhận module đã được gỡ.
## 4.3. Kết quả procfs
Driver Task 2 tạo `/proc/counter_info` để hiển thị thông tin thống kê.

Nội dung procfs gồm:
- `Counter value`: giá trị hiện tại của counter.
- `Total reads`: tổng số lần đọc.
- `Total writes`: tổng số lần ghi.

Kết quả kiểm thử trước đó cho thấy procfs hiển thị:
- `Counter value : 1`
- `Total reads : 4`
- `Total writes : 2`
Các giá trị thống kê thay đổi theo những thao tác đọc và ghi được thực hiện trên thiết bị.
### Kết quả tại ba thời điểm

**Lần 1 — Trước khi ghi dữ liệu:**

```text
Counter value : 0
Total reads   : 0
Total writes  : 0
```

**Lần 2 — Sau khi ghi giá trị 100:**

Thực hiện lệnh `echo 100 > /dev/counter`.

```text
Counter value : 100
Total reads   : 0
Total writes  : 1
```

**Lần 3 — Sau khi reset counter:**

Thực hiện lệnh `echo 0 > /dev/counter`.

```text
Counter value : 0
Total reads   : 0
Total writes  : 2
```

Kết quả cho thấy counter được cập nhật theo dữ liệu ghi vào thiết bị.
Số lần ghi tăng sau mỗi thao tác ghi, còn số lần đọc vẫn bằng 0
trong các lần kiểm tra trên.
## 4.4. Lỗi gặp phải và cách khắc phục
### Lỗi 1: Thiết bị chưa tồn tại

Khi kiểm tra `/dev/counter`, hệ thống báo `No such file or directory`.

**Nguyên nhân:** Driver đã đăng ký thiết bị nhưng device node chưa được tạo.

**Khắc phục:** Dùng `mknod /dev/counter c 10 125` để tạo device node.

### Lỗi 2: Ghi dữ liệu không hợp lệ

Khi ghi `-1` hoặc `abc` vào `/dev/counter`, shell báo `Invalid argument`.

**Nguyên nhân:** Dữ liệu đầu vào không đáp ứng yêu cầu của driver.

**Khắc phục:** Sử dụng giá trị hợp lệ theo cách xử lý dữ liệu trong driver.
---

# 5. Kết luận
Qua Assignment 1, hai loại character device driver và misc device driver đã được tìm hiểu, biên dịch và kiểm thử trên môi trường QEMU ARM.

Task 1 minh họa cách đăng ký character device bằng `register_chrdev()`, xử lý các thao tác đọc/ghi và triển khai ioctl để điều khiển thiết bị.

Task 2 minh họa việc sử dụng `misc_register()`, `atomic_t` để quản lý counter và procfs để hiển thị thông tin thống kê.

Các kết quả kiểm thử giúp làm rõ luồng tương tác giữa ứng dụng userspace và driver trong kernel Linux.


