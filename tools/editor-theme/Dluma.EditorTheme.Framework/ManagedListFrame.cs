using System;
using System.Windows.Forms;

namespace Dluma.EditorTheme.Framework
{
    internal sealed class ManagedListFrame : IDisposable
    {
        private readonly ListView list;
        private BorderStyle originalBorder;
        private bool enabled, ownsBorder, relinquished, changing, disposed;

        internal ManagedListFrame(ListView value)
        {
            list = value;
            list.HandleCreated += HandleCreated;
            list.StyleChanged += StyleChanged;
        }

        internal void Apply(ThemeSnapshot theme)
        {
            if (disposed || list.IsDisposed) return;
            enabled = theme.Kind != 2;
            UpdateBorder();
        }

        private bool Eligible
        {
            get
            {
                return list.GetType() == typeof(ListView) && list.View == View.Details &&
                    !list.CheckBoxes && list.SmallImageList == null && list.LargeImageList == null;
            }
        }

        private void HandleCreated(object sender, EventArgs e) { UpdateBorder(); }
        private void StyleChanged(object sender, EventArgs e) { UpdateBorder(); }

        private void UpdateBorder()
        {
            if (disposed || changing || list.IsDisposed ||
                (list.IsHandleCreated && list.InvokeRequired)) return;
            if (ownsBorder && list.BorderStyle != BorderStyle.None)
            {
                ownsBorder = false;
                relinquished = true;
                return;
            }
            if (!enabled || !Eligible) { RestoreBorder(); return; }
            if (ownsBorder || relinquished || list.BorderStyle != BorderStyle.Fixed3D) return;

            // Remove the native raised edge instead of repainting it after Windows.
            // The managed style also survives recreation of the native list handle.
            originalBorder = list.BorderStyle;
            ownsBorder = true;
            changing = true;
            try { list.BorderStyle = BorderStyle.None; }
            catch { ownsBorder = list.BorderStyle == BorderStyle.None; throw; }
            finally { changing = false; }
        }

        private void RestoreBorder()
        {
            if (!ownsBorder || changing) return;
            ownsBorder = false;
            if (list.IsDisposed || list.BorderStyle != BorderStyle.None) return;
            changing = true;
            try { list.BorderStyle = originalBorder; }
            finally { changing = false; }
        }

        public void Dispose()
        {
            if (disposed) return;
            disposed = true;
            enabled = false;
            list.HandleCreated -= HandleCreated;
            list.StyleChanged -= StyleChanged;
            RestoreBorder();
        }
    }
}
