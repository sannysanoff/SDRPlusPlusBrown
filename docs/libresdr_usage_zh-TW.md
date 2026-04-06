# LibreSDR 使用說明（繁體中文）

這份文件是給 LibreSDR 使用者的操作說明，重點放在「怎麼在這個 LibreSDR 專用版 SDR++ Brown 上正常使用」。

如果你要重建 Windows 環境、重新編譯或重新打包，請看：

* [`docs/libresdr_windows_build.md`](docs/libresdr_windows_build.md)

## 這個分支解決了什麼

這個分支主要針對 LibreSDR 在 Windows 上的 Pluto source 使用體驗做修正：

* 改善裝置辨識，不再只靠單一描述字串
* 當 `iio_scan_context` 在 Windows 找不到板子時，會額外直接嘗試常見 URI
* 支援 `CS16` 與 `CS8` IQ 模式
* 支援在程式運行中切換 sample rate，不用整個重開 SDR++
* 保留同一台 LibreSDR 在 URI 正規化後的原有設定

## 適用情境

這個分支適合下面這類 LibreSDR 使用方式：

* LibreSDR 以 PlutoSDR 相容 IIO 拓樸對外提供裝置
* Windows 上 `iio_info -u ip:192.168.2.1` 可以正常連到板子
* 你希望在 SDR++ Brown 裡直接把板子當 Pluto 類設備使用

## 韌體建議

如果你要使用 8-bit 高頻寬模式，建議搭配：

* [`F5OEO/tezuka_fw`](https://github.com/F5OEO/tezuka_fw)

這套韌體提供 LibreSDR / ZynqSDR 的 complex 8-bit 串流模式。這個分支已經把 SDR++ 端的 `CS8` 路徑接好，適合拿來搭配 `tezuka_fw` 使用。

依實際網路、主機效能與韌體設定不同，可用頻寬會有差異；這個分支是以 LibreSDR + `tezuka_fw` 的 8-bit / 40 MHz 使用目標來整理的。

## 啟動前檢查

在啟動 SDR++ 前，建議先確認：

1. 板子已經上電，並且網路可通。
2. 你知道板子的 IP，常見是 `192.168.2.1`。
3. 用 `iio_info` 可以直接連上，例如：

```powershell
iio_info -u ip:192.168.2.1
```

如果 `iio_info -s` 掃不到裝置，但 `iio_info -u ip:192.168.2.1` 可以，這個分支仍然能處理，因為它會在 Windows 上額外直接 probe 常見 URI。

## 第一次使用

1. 啟動這個分支編出的 SDR++ Brown。
2. 在 Source 選擇 `PlutoSDR`。
3. 進入 Pluto source 選單。
4. 確認裝置清單中有你的 LibreSDR。
5. 先用 `CS16` 測試基本收訊。
6. 確認播放後頻譜與瀑布圖正常更新。
7. 再切到 `CS8` 測試 8-bit 串流模式。

## IQ Mode 說明

Pluto / LibreSDR source 選單裡現在有：

* `CS16`
* `CS8`

### 什麼時候用 `CS16`

`CS16` 比較適合：

* 先驗證基本連線是否正常
* 不需要極寬頻寬
* 想先排除韌體或網路設定問題

### 什麼時候用 `CS8`

`CS8` 比較適合：

* 搭配 `tezuka_fw`
* 想把主機端串流頻寬往上拉
* 目標是 8-bit / 高頻寬使用情境

## Sample Rate 熱切

這個分支已經支援在 source 選單中切換 sample rate，而不用整個重開 SDR++。

實際行為是：

* 變更 sample rate
* 模組內部重新套用輸入取樣率
* 若正在播放，會重新啟動 Pluto / LibreSDR 串流路徑並保留目前頻率

所以如果你只是要測不同頻寬，不需要每次把整個軟體關掉重開。

## 設定保存

這個分支會保存每台 LibreSDR 的個別設定，例如：

* sample rate
* bandwidth
* gain mode
* gain
* IQ mode

另外也處理了同一台板子因為 URI 正規化而改名的情況。例如原本是：

* `[ip:libresdr.local]`

後來被正規化成：

* `[ip:192.168.2.1]`

這種情況下，原來的裝置設定會被接續使用，不會因為字串變動而掉回預設值。

## 如果按下播放沒反應

請先依序檢查：

1. `iio_info -u ip:192.168.2.1` 是否可用。
2. 韌體是否正確，尤其是你要使用 `CS8` 時。
3. Source 是否選到 `PlutoSDR`，裝置是否真的選到 LibreSDR。
4. 先切回 `CS16` 測試，確認不是 8-bit 韌體或網路帶寬造成的問題。
5. sample rate 先降到較保守的值，再往上加。

## 建議測試順序

比較穩的測試方式是：

1. 先確認 `iio_info -u ip:192.168.2.1` 正常。
2. 在 SDR++ 裡用 `CS16` + 較保守 sample rate 驗證基本功能。
3. 再切到 `CS8`。
4. 再逐步提高 sample rate，測試你的主機與網路能承受的範圍。
5. 最後再調整 bandwidth、gain mode 與其他使用習慣。

## 打包版說明

這個分支也更新了 Windows 打包流程：

* 會從實際 build 輸出收集模組
* 會補上本機需要的執行期 DLL
* 避免「編譯成功但打包帶到舊 DLL」的問題

如果你要重新打包，請看：

* [`make_windows_package.ps1`](../make_windows_package.ps1)
* [`docs/libresdr_windows_build.md`](docs/libresdr_windows_build.md)

## 總結

如果你是 LibreSDR 使用者，這個分支的定位可以簡單理解成：

* 以 Pluto 相容路徑接 LibreSDR
* 補強 Windows 掃描與連線穩定性
* 支援 `CS8`
* 方便搭配 `tezuka_fw`
* 讓 sample rate 切換與設定保存更符合實際使用需求
