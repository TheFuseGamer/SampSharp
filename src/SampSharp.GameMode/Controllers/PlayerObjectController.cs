﻿// SampSharp
// Copyright (C) 2014 Tim Potze
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
// 
// For more information, please refer to <http://unlicense.org>

using SampSharp.GameMode.Definitions;
using SampSharp.GameMode.World;

namespace SampSharp.GameMode.Controllers
{
    /// <summary>
    ///     A controller processing all player-object actions.
    /// </summary>
    public class PlayerObjectController : IEventListener, ITypeProvider
    {
        /// <summary>
        ///     Registers the events this PlayerObjectController wants to listen to.
        /// </summary>
        /// <param name="gameMode">The running GameMode.</param>
        public virtual void RegisterEvents(BaseMode gameMode)
        {
            gameMode.PlayerObjectMoved += (sender, args) =>
            {
                var obj = PlayerObject.Find(Player.Find(args.PlayerId), args.ObjectId);
                if (obj != null)
                    obj.OnMoved(args);
            };
            gameMode.PlayerEditObject += (sender, args) =>
            {
                if (args.ObjectType == ObjectType.PlayerObject)
                {
                    var obj = PlayerObject.Find(Player.Find(args.PlayerId), args.ObjectId);

                    if (obj != null)
                        obj.OnEdited(args);
                }
            };
            gameMode.PlayerSelectObject += (sender, args) =>
            {
                if (args.ObjectType == ObjectType.PlayerObject)
                {
                    var obj = PlayerObject.Find(Player.Find(args.PlayerId), args.ObjectId);

                    if (obj != null)
                        obj.OnSelected(args);
                }
            };
        }

        /// <summary>
        ///     Registers types this PlayerObjectController requires the system to use.
        /// </summary>
        public virtual void RegisterTypes()
        {
            PlayerObject.Register<PlayerObject>();
        }
    }
}