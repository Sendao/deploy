#include "deploy.hh"


// Grub hash password protector:
const char *scriptext = /*"\
#!/bin/bash\n\
# Config\n\
#segment-repeat=0\n\
#line-repeat=0\n\
\n\
# Step 0: input password\n\
input Enter the new password: =arg1\n\
\n\
# Step 1\n\
echo Setting password: ${arg1}\n\
[ -e /etc/grub.d/script_copier ] || cp /etc/grub.d/40_custom /etc/grub.d/script_copier\n\
\n\
# Step 2\n\
grub2-mkpasswd-pbkdf2\n\
#response password:=${arg1}\n\
#bscan hash of your password is =pwdhash\n\
\n\
# Step 3\n\
rm -f /etc/grub.d/40_custom\n\
cp -f /etc/grub.d/script_copier /etc/grub.d/40_custom\n\
echo set superusers=\"root\" >> /etc/grub.d/40_custom\n\
echo password_pbkdf2 root ${pwdhash} >> /etc/grub.d/40_custom\n\
\n\
# Step 4\n\
grub2-mkconfig -o /boot/grub2/grub.cfg\n\
";
*/ NULL;


