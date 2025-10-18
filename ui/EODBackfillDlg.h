#ifndef EOD_BACKFILL_DLG_H
#define EOD_BACKFILL_DLG_H

#include <windows.h>

class CEODBackfillDlg
{
public:
    // Fungsi utama untuk menampilkan dialog
    void DoModal(HWND hParent);

private:
    // Prosedur dialog statis yang dipanggil oleh Windows
    static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

    // Fungsi internal untuk menangani pesan-pesan dialog
    BOOL OnInitDialog(HWND hDlg);
    void OnFetchClicked(HWND hDlg);
};

#endif // EOD_BACKFILL_DLG_H