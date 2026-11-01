use Time::HiRes qw(time);
open F,"/root" or die; my $fd=fileno F; my $b="\0"x256; my $em=""; my $r1="/root"; my $ex="/proc/self/exe"; my $xn="user.x";
my @t=([getpid=>sub{syscall(172)}],[fstat80=>sub{syscall(80,$fd,$b)}],
[fstatat_empty=>sub{syscall(79,$fd,$em,$b,0x1000)}],[newfstatat_path=>sub{syscall(79,-100,$r1,$b,0)}],
[statx_fd=>sub{syscall(291,$fd,$em,0x1000,0x7ff,$b)}],[faccessat=>sub{syscall(48,-100,$r1,0)}],
[readlinkat=>sub{my $o="\0"x256; syscall(78,-100,$ex,$o,255)}],
[openat_close=>sub{my $x=syscall(56,-100,$r1,0,0); syscall(57,$x)}],[getxattr=>sub{my $o="\0"x64; syscall(8,$r1,$xn,$o,64)}],
[lseek=>sub{syscall(62,$fd,0,0)}],[dup_close=>sub{my $x=syscall(23,$fd); syscall(57,$x)}],[getuid=>sub{syscall(174)}]);
for my $t(@t){my($n,$f)=@$t; $!=0; my $r=eval{$f->()}; my $e="$! $@"; my $s=time; eval{$f->() for 1..20000}; printf "%-16s r=%s %s %.0f ns\n",$n,$r//'X',$e,(time-$s)*5e4}
