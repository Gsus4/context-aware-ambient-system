# GitHub 上傳前檢查清單

## 不要上傳

- `.env` 實際設定檔
- Wi-Fi SSID / password
- MQTT 帳號密碼
- 私人 IP 或公司 / 學校內網固定 IP
- build 產物，例如 `build/`、`.elf`、`.bin`、`.hex`、`.uf2`
- 執行 log、暫存檔、壓縮備份檔
- 個人履歷、電話、住址或完整學員資料表

## 建議保留

- 原始碼
- README
- 系統架構圖
- MQTT / UART 規格文件
- 測試方式說明
- 問題排查紀錄
- `.env.example`

## 上傳流程

```bash
git init
git add .
git commit -m "Initial public portfolio version"
git branch -M main
git remote add origin <your-github-repo-url>
git push -u origin main
```

## 後續待補

- Demo 影片連結
- Dashboard 截圖
- 實機接線圖
- 面試用專題摘要
