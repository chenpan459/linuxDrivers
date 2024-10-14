---
layout: card
title: "IOCrest JMB582 PCIe Gen3 SATA Controller"
picture: "/images/storage-iocrest-sata-2-port-jmb582.jpg"
functionality: "None"
driver_required: "Yes"
github_issue: "https://github.com/geerlingguy/raspberry-pi-pcie-devices/issues/64"
buy_link: https://amzn.to/3tmBsUU
videos: []
---
Since mid-2021, [SATA support is built into the Raspberry Pi kernel](https://www.jeffgeerling.com/blog/2021/raspberry-pi-os-now-has-sata-support-built), so assuming you have updated to the latest version (`sudo apt upgrade -y`), this card _should_ work out of the box.

However, the Pi kernel panics any time this particular controller is initialized; see the linked GitHub issue for more details.
